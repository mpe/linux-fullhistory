/*
 * platinum.c: Console support for PowerMac "platinum" display adaptor.
 *
 * Copyright (C) 1996 Paul Mackerras and Mark Abene.
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
#include <linux/nvram.h>
#include <asm/vc_ioctl.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/init.h>
#include <linux/selection.h>
#include "pmac-cons.h"
#include "platinum.h"
#include <linux/console_compat.h>

/*
 * Structure of the registers for the DACula colormap device.
 */
struct cmap_regs {
	unsigned char addr;
	char pad1[15];
	unsigned char d1;
	char pad2[15];
	unsigned char d2;
	char pad3[15];
	unsigned char lut;
	char pad4[15];
};

/*
 * Structure of the registers for the "platinum" display adaptor.
 */
#define PAD(x)	char x[12]

struct preg {			/* padded register */
	unsigned r;
	char pad[12];
};

struct platinum_regs {
	struct preg reg[128];
};

static void set_platinum_clock(unsigned char *clksel);
static int read_platinum_sense(void);
static int platinum_vram_reqd(int vmode, int cmode);

static int total_vram;		/* total amount of video memory, bytes */
static unsigned char *frame_buffer;
static unsigned char *base_frame_buffer;
static struct cmap_regs *cmap_regs;
static volatile struct platinum_regs *plat_regs;

static unsigned long frame_buffer_phys;
static unsigned long cmap_regs_phys;
static unsigned long plat_regs_phys;

/*
 * Register initialization tables for the platinum display.
 *
 * It seems that there are two different types of platinum display
 * out there.  Older ones use the values in clocksel[1], for which
 * the formula for the clock frequency seems to be
 *	F = 14.3MHz * c0 / (c1 & 0x1f) / (1 << (c1 >> 5))
 * Newer ones use the values in clocksel[0], for which the formula
 * seems to be
 *	F = 15MHz * c0 / ((c1 & 0x1f) + 2) / (1 << (c1 >> 5))
 */
struct plat_regvals {
	int	fb_offset;
	int	pitch[3];
	unsigned regs[26];
	unsigned char plat_offset[3];
	unsigned char mode[3];
	unsigned char dacula_ctrl[3];
	unsigned char clocksel[2][2];
};

#define DIV2	0x20
#define DIV4	0x40
#define DIV8	0x60
#define DIV16	0x80

/* 1280x1024, 75Hz (20) */
static struct plat_regvals platinum_reg_init_20 = {
	0x5c00,
	{ 1312, 2592, 2592 },
	{ 0xffc, 4, 0, 0, 0, 0, 0x428, 0,
	  0, 0xb3, 0xd3, 0x12, 0x1a5, 0x23, 0x28, 0x2d,
	  0x5e, 0x19e, 0x1a4, 0x854, 0x852, 4, 9, 0x50,
	  0x850, 0x851 }, { 0x58, 0x5d, 0x5d },
	{ 0, 0xff, 0xff }, { 0x51, 0x55, 0x55 },
	{{ 45, 3 }, { 66, 7 }}
};

/* 1280x960, 75Hz (19) */
static struct plat_regvals platinum_reg_init_19 = {
	0x5c00,
	{ 1312, 2592, 2592 },
	{ 0xffc, 4, 0, 0, 0, 0, 0x428, 0,
	  0, 0xb2, 0xd2, 0x12, 0x1a3, 0x23, 0x28, 0x2d,
	  0x5c, 0x19c, 0x1a2, 0x7d0, 0x7ce, 4, 9, 0x4c,
	  0x7cc, 0x7cd }, { 0x56, 0x5b, 0x5b },
	{ 0, 0xff, 0xff }, { 0x51, 0x55, 0x55 },
	{{ 42, 3 }, { 44, 5 }}
};

/* 1152x870, 75Hz (18) */
static struct plat_regvals platinum_reg_init_18 = {
	0x11b0,
	{ 1184, 2336, 4640 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x38f, 0,
	  0, 0x294, 0x16c, 0x20, 0x2d7, 0x3f, 0x49, 0x53,
	  0x82, 0x2c2, 0x2d6, 0x726, 0x724, 4, 9, 0x52,
	  0x71e, 0x722 }, { 0x74, 0x7c, 0x81 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 26, 0 + DIV2 }, { 42, 6 }}
};

/* 1024x768, 75Hz (17) */
static struct plat_regvals platinum_reg_init_17 = {
	0x10b0,
	{ 1056, 2080, 4128 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x254, 0x14b, 0x18, 0x295, 0x2f, 0x32, 0x3b,
	  0x80, 0x280, 0x296, 0x648, 0x646, 4, 9, 0x40,
	  0x640, 0x644 }, { 0x72, 0x7a, 0x7f },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 54, 3 + DIV2 }, { 67, 12 }}
};

/* 1024x768, 75Hz (16) */
static struct plat_regvals platinum_reg_init_16 = {
	0x10b0,
	{ 1056, 2080, 4128 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x250, 0x147, 0x17, 0x28f, 0x2f, 0x35, 0x47,
	  0x82, 0x282, 0x28e, 0x640, 0x63e, 4, 9, 0x3c,
	  0x63c, 0x63d }, { 0x74, 0x7c, 0x81 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 20, 0 + DIV2 }, { 11, 2 }}
};

/* 1024x768, 70Hz (15) */
static struct plat_regvals platinum_reg_init_15 = {
	0x10b0,
	{ 1056, 2080, 4128 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x254, 0x14b, 0x22, 0x297, 0x43, 0x49, 0x5b,
	  0x86, 0x286, 0x296, 0x64c, 0x64a, 0xa, 0xf, 0x44,
	  0x644, 0x646 }, { 0x78, 0x80, 0x85 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 19, 0 + DIV2 }, { 110, 21 }}
};

/* 1024x768, 60Hz (14) */
static struct plat_regvals platinum_reg_init_14 = {
	0x10b0,
	{ 1056, 2080, 4128 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x25a, 0x14f, 0x22, 0x29f, 0x43, 0x49, 0x5b,
	  0x8e, 0x28e, 0x29e, 0x64c, 0x64a, 0xa, 0xf, 0x44,
	  0x644, 0x646 }, { 0x80, 0x88, 0x8d },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 71, 6 + DIV2 }, { 118, 13 + DIV2 }}
};

/* 832x624, 75Hz (13) */
static struct plat_regvals platinum_reg_init_13 = {
	0x70,
	{ 864, 1680, 3360 },	/* MacOS does 1680 instead of 1696 to fit 16bpp in 1MB */
	{ 0xff0, 4, 0, 0, 0, 0, 0x299, 0,
	  0, 0x21e, 0x120, 0x10, 0x23f, 0x1f, 0x25, 0x37,
	  0x8a, 0x22a, 0x23e, 0x536, 0x534, 4, 9, 0x52,
	  0x532, 0x533 }, { 0x7c, 0x84, 0x89 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 30, 0 + DIV4 }, { 56, 7 + DIV2 }}
};

/* 800x600, 75Hz (12) */
static struct plat_regvals platinum_reg_init_12 = {
	0x1010,
	{ 832, 1632, 3232 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x1ce, 0x108, 0x14, 0x20f, 0x27, 0x30, 0x39,
	  0x72, 0x202, 0x20e, 0x4e2, 0x4e0, 4, 9, 0x2e,
	  0x4de, 0x4df }, { 0x64, 0x6c, 0x71 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 122, 7 + DIV4 }, { 62, 9 + DIV2 }}
};

/* 800x600, 72Hz (11) */
static struct plat_regvals platinum_reg_init_11 = {
	0x1010,
	{ 832, 1632, 3232 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x1ca, 0x104, 0x1e, 0x207, 0x3b, 0x44, 0x4d,
	  0x56, 0x1e6, 0x206, 0x534, 0x532, 0xa, 0xe, 0x38,
	  0x4e8, 0x4ec }, { 0x48, 0x50, 0x55 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 26, 0 + DIV4 }, { 42, 6 + DIV2 }}
};

/* 800x600, 60Hz (10) */
static struct plat_regvals platinum_reg_init_10 = {
	0x1010,
	{ 832, 1632, 3232 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x1ce, 0x108, 0x20, 0x20f, 0x3f, 0x45, 0x5d,
	  0x66, 0x1f6, 0x20e, 0x4e8, 0x4e6, 6, 0xa, 0x34,
	  0x4e4, 0x4e5 }, { 0x58, 0x60, 0x65 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 54, 3 + DIV4 }, { 95, 1 + DIV8 }}
};

/* 800x600, 56Hz (9) --unsupported? copy of mode 10 for now... */
static struct plat_regvals platinum_reg_init_9 = {
	0x1010,
	{ 832, 1632, 3232 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x1ce, 0x108, 0x20, 0x20f, 0x3f, 0x45, 0x5d,
	  0x66, 0x1f6, 0x20e, 0x4e8, 0x4e6, 6, 0xa, 0x34,
	  0x4e4, 0x4e5 }, { 0x58, 0x60, 0x65 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 54, 3 + DIV4 }, { 88, 1 + DIV8 }}
};

/* 768x576, 50Hz Interlaced-PAL (8) */
static struct plat_regvals platinum_reg_init_8 = {
	0x1010,
	{ 800, 1568, 3104 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0xc8, 0xec, 0x11, 0x1d7, 0x22, 0x25, 0x36,
	  0x47, 0x1c7, 0x1d6, 0x271, 0x270, 4, 9, 0x27,
	  0x267, 0x26b }, { 0x39, 0x41, 0x46 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 31, 0 + DIV16 }, { 74, 9 + DIV8 }}
};

/* 640x870, 75Hz Portrait (7) */
static struct plat_regvals platinum_reg_init_7 = {
	0xb10,
	{ 672, 1312, 2592 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x176, 0xd0, 0x14, 0x19f, 0x27, 0x2d, 0x3f,
	  0x4a, 0x18a, 0x19e, 0x72c, 0x72a, 4, 9, 0x58,
	  0x724, 0x72a }, { 0x3c, 0x44, 0x49 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 30, 0 + DIV4 }, { 56, 7 + DIV2 }}
};

/* 640x480, 67Hz (6) */
static struct plat_regvals platinum_reg_init_6 = {
	0x1010,
	{ 672, 1312, 2592 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x209, 0,
	  0, 0x18e, 0xd8, 0x10, 0x1af, 0x1f, 0x25, 0x37,
	  0x4a, 0x18a, 0x1ae, 0x41a, 0x418, 4, 9, 0x52,
	  0x412, 0x416 }, { 0x3c, 0x44, 0x49 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 99, 4 + DIV8 }, { 42, 5 + DIV4 }}
};

/* 640x480, 60Hz (5) */
static struct plat_regvals platinum_reg_init_5 = {
	0x1010,
	{ 672, 1312, 2592 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x15e, 0xc8, 0x18, 0x18f, 0x2f, 0x35, 0x3e,
	  0x42, 0x182, 0x18e, 0x41a, 0x418, 2, 7, 0x44,
	  0x404, 0x408 }, { 0x34, 0x3c, 0x41 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 26, 0 + DIV8 }, { 14, 2 + DIV4 }}
};

/* 640x480, 60Hz Interlaced-NTSC (4) */
static struct plat_regvals platinum_reg_init_4 = {
	0x1010,
	{ 672, 1312, 2592 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0xa5, 0xc3, 0xe, 0x185, 0x1c, 0x1f, 0x30,
	  0x37, 0x177, 0x184, 0x20d, 0x20c, 5, 0xb, 0x23,
	  0x203, 0x206 }, { 0x29, 0x31, 0x36 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 94, 5 + DIV16 }, { 48, 7 + DIV8 }}
};

/* 640x480, 50Hz Interlaced-PAL (3) */
static struct plat_regvals platinum_reg_init_3 = {
	0x1010,
	{ 672, 1312, 2592 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0xc8, 0xec, 0x11, 0x1d7, 0x22, 0x25, 0x36,
	  0x67, 0x1a7, 0x1d6, 0x271, 0x270, 4, 9, 0x57,
	  0x237, 0x26b }, { 0x59, 0x61, 0x66 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 31, 0 + DIV16 }, { 74, 9 + DIV8 }}
};

/* 512x384, 60Hz (2) */
static struct plat_regvals platinum_reg_init_2 = {
	0x1010,
	{ 544, 1056, 2080 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0x25c, 0x140, 0x10, 0x27f, 0x1f, 0x2b, 0x4f,
	  0x68, 0x268, 0x27e, 0x32e, 0x32c, 4, 9, 0x2a,
	  0x32a, 0x32b }, { 0x5a, 0x62, 0x67 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 33, 2 + DIV8 }, { 79, 9 + DIV8 }}
};

/* 512x384, 60Hz Interlaced-NTSC (1) */
static struct plat_regvals platinum_reg_init_1 = {
	0x1010,
	{ 544, 1056, 2080 },
	{ 0xff0, 4, 0, 0, 0, 0, 0x320, 0,
	  0, 0xa5, 0xc3, 0xe, 0x185, 0x1c, 0x1f, 0x30,
	  0x57, 0x157, 0x184, 0x20d, 0x20c, 5, 0xb, 0x53,
	  0x1d3, 0x206 }, { 0x49, 0x51, 0x56 },
	{ 2, 0, 0xff }, { 0x11, 0x15, 0x19 },
	{{ 94, 5 + DIV16 }, { 48, 7 + DIV8 }}
};

static struct plat_regvals *platinum_reg_init[VMODE_MAX] = {
	&platinum_reg_init_1,
	&platinum_reg_init_2,
	&platinum_reg_init_3,
	&platinum_reg_init_4,
	&platinum_reg_init_5,
	&platinum_reg_init_6,
	&platinum_reg_init_7,
	&platinum_reg_init_8,
	&platinum_reg_init_9,
	&platinum_reg_init_10,
	&platinum_reg_init_11,
	&platinum_reg_init_12,
	&platinum_reg_init_13,
	&platinum_reg_init_14,
	&platinum_reg_init_15,
	&platinum_reg_init_16,
	&platinum_reg_init_17,
	&platinum_reg_init_18,
	&platinum_reg_init_19,
	&platinum_reg_init_20
};

__openfirmware

/*
 * Get the monitor sense value.
 */
static int
read_platinum_sense()
{
	int sense;

	plat_regs->reg[23].r = 7;	/* turn off drivers */
	eieio(); __delay(2000);
	sense = (~plat_regs->reg[23].r & 7) << 8;

	/* drive each sense line low in turn and collect the other 2 */
	plat_regs->reg[23].r = 3;	/* drive A low */
	eieio(); __delay(2000);
	sense |= (~plat_regs->reg[23].r & 3) << 4;
	eieio();
	plat_regs->reg[23].r = 5;	/* drive B low */
	eieio(); __delay(2000);
	sense |= (~plat_regs->reg[23].r & 4) << 1;
	sense |= (~plat_regs->reg[23].r & 1) << 2;
	eieio();
	plat_regs->reg[23].r = 6;	/* drive C low */
	eieio(); __delay(2000);
	sense |= (~plat_regs->reg[23].r & 6) >> 1;
	eieio();

	plat_regs->reg[23].r = 7;	/* turn off drivers */
	return sense;
}

static inline int platinum_vram_reqd(int vmode, int cmode)
{
        return vmode_attrs[vmode-1].vres
                * platinum_reg_init[vmode-1]->pitch[cmode];
}

void
map_platinum(struct device_node *dp)
{
	int i, sense;
	unsigned long addr, size;
	int bank0, bank1, bank2, bank3;

	if (dp->next != 0)
		printk("Warning: only using first platinum display device\n");
	if (dp->n_addrs != 2)
		panic("expecting 2 addresses for platinum (got %d)",
		      dp->n_addrs);

	/* Map in frame buffer and registers */
	for (i = 0; i < dp->n_addrs; ++i) {
		addr = dp->addrs[i].address;
		size = dp->addrs[i].size;
		if (size >= 0x400000) {
			/* frame buffer - map only 4MB */
			frame_buffer_phys = addr;
			frame_buffer = __ioremap(addr, 0x400000, _PAGE_WRITETHRU);
			base_frame_buffer = frame_buffer;
		} else {
			/* registers */
			plat_regs_phys = addr;
			plat_regs = ioremap(addr, size);
		}
	}
	cmap_regs_phys = 0xf301b000;	/* XXX not in prom? */
	cmap_regs = ioremap(cmap_regs_phys, 0x1000);

	/* Grok total video ram */
	plat_regs->reg[16].r = (unsigned)frame_buffer_phys;
	plat_regs->reg[20].r = 0x1011;	/* select max vram */
	plat_regs->reg[24].r = 0;	/* switch in vram */
	eieio();
	frame_buffer[0x100000] = 0x34;
	frame_buffer[0x200000] = 0x56;
	frame_buffer[0x300000] = 0x78;
	eieio();
	bank0 = 1; /* builtin 1MB vram, always there */
	bank1 = frame_buffer[0x100000] == 0x34;
	bank2 = frame_buffer[0x200000] == 0x56;
	bank3 = frame_buffer[0x300000] == 0x78;
	total_vram = (bank0 + bank1 + bank2 + bank3) * 0x100000;
	printk("Total VRAM = %dMB\n", total_vram / 1024 / 1024);

	sense = read_platinum_sense();
	if (video_mode == VMODE_NVRAM) {
		video_mode = nvram_read_byte(NV_VMODE);
		if (video_mode <= 0 || video_mode > VMODE_MAX
		    || platinum_reg_init[video_mode-1] == 0)
			video_mode = VMODE_CHOOSE;
	}
	if (video_mode == VMODE_CHOOSE)
		video_mode = map_monitor_sense(sense);
	if (platinum_reg_init[video_mode-1] == 0)
		video_mode = VMODE_640_480_60;
	printk("Monitor sense value = 0x%x, ", sense);

	if (color_mode == CMODE_NVRAM)
                color_mode = nvram_read_byte(NV_CMODE);
        if (color_mode < CMODE_8 || color_mode > CMODE_32)
                color_mode = CMODE_8;
	/*
	 * Reduce the pixel size if we don't have enough VRAM.
	 */
	while (color_mode > CMODE_8
	       && platinum_vram_reqd(video_mode, color_mode) > total_vram)
		--color_mode;
	/*
	 * Reduce the video mode if we don't have enough VRAM.
	 */
	while (platinum_vram_reqd(video_mode, color_mode) > total_vram)
		--video_mode;
}

#define STORE_D2(a, v) { \
	out_8(&cmap_regs->addr, (a+32)); \
	out_8(&cmap_regs->d2, (v)); \
}

static void
set_platinum_clock(unsigned char *clksel)
{
	STORE_D2(6, 0xc6);
	out_8(&cmap_regs->addr, 3+32);
	if (cmap_regs->d2 == 2) {
		STORE_D2(7, clksel[0]);
		STORE_D2(8, clksel[1]);
		STORE_D2(3, 3);
	} else {
		STORE_D2(4, clksel[0]);
		STORE_D2(5, clksel[1]);
		STORE_D2(3, 2);
	}
	__delay(5000);
	STORE_D2(9, 0xa6);
}

void
platinum_init()
{
	int i, yoff, width, clkmode, dtype;
	struct plat_regvals *init;
	unsigned *p;
	int one_mb = 0;

	if (total_vram == 0x100000) one_mb=1;

	if (video_mode <= 0 || video_mode > VMODE_MAX
	    || (init = platinum_reg_init[video_mode-1]) == 0)
		panic("platinum: video mode %d not supported", video_mode);

	frame_buffer = base_frame_buffer + init->fb_offset;
	/* printk("Frame buffer start address is %p\n", frame_buffer); */

	pixel_size = 1 << color_mode;
	line_pitch = init->pitch[color_mode];
	width = vmode_attrs[video_mode-1].hres;
	n_scanlines = vmode_attrs[video_mode-1].vres;
	row_pitch = line_pitch * 16;

	/* Initialize display timing registers */
	out_be32(&plat_regs->reg[24].r, 7);	/* turn display off */

	for (i = 0; i < 26; ++i)
		plat_regs->reg[i+32].r = init->regs[i];
	plat_regs->reg[26+32].r = (one_mb ? init->plat_offset[color_mode] + 4 - color_mode : init->plat_offset[color_mode]);
	plat_regs->reg[16].r = (unsigned) frame_buffer_phys + init->fb_offset;
	plat_regs->reg[18].r = line_pitch;
	plat_regs->reg[19].r = (one_mb ? init->mode[color_mode+1] : init->mode[color_mode]);
	plat_regs->reg[20].r = (one_mb ? 0x11 : 0x1011);
	plat_regs->reg[21].r = 0x100;
	plat_regs->reg[22].r = 1;
	plat_regs->reg[23].r = 1;
	plat_regs->reg[26].r = 0xc00;
	plat_regs->reg[27].r = 0x235;
	/* plat_regs->reg[27].r = 0x2aa; */

	STORE_D2(0, (one_mb ? init->dacula_ctrl[color_mode] & 0xf : init->dacula_ctrl[color_mode]));
	STORE_D2(1, 4);
	STORE_D2(2, 0);
	/*
	 * Try to determine whether we have an old or a new DACula.
	 */
	out_8(&cmap_regs->addr, 0x40);
	dtype = cmap_regs->d2;
	switch (dtype) {
	case 0x3c:
		clkmode = 1;
		break;
	case 0x84:
		clkmode = 0;
		break;
	default:
		clkmode = 0;
		printk("Unknown DACula type: %x\n", cmap_regs->d2);
	}
	set_platinum_clock(init->clocksel[clkmode]);

	out_be32(&plat_regs->reg[24].r, 0);	/* turn display on */

	pmac_init_palette();

	yoff = (n_scanlines % 16) / 2;
	fb_start = frame_buffer + yoff * line_pitch + 0x10;

	/* Clear screen */
	p = (unsigned *) (frame_buffer + 0x10);
	for (i = n_scanlines * line_pitch / sizeof(unsigned); i != 0; --i)
		*p++ = 0;

	display_info.height = n_scanlines;
	display_info.width = width;
	display_info.depth = 8 * pixel_size;
	display_info.pitch = line_pitch;
	display_info.mode = video_mode;
	strncpy(display_info.name, "platinum", sizeof(display_info.name));
	display_info.fb_address = frame_buffer_phys + init->fb_offset + 0x10;
	display_info.cmap_adr_address = cmap_regs_phys;
	display_info.cmap_data_address = cmap_regs_phys + 0x30;
	display_info.disp_reg_address = plat_regs_phys;
}

int
platinum_setmode(struct vc_mode *mode, int doit)
{
	int cmode;

	if (mode->mode <= 0 || mode->mode > VMODE_MAX
	    || platinum_reg_init[mode->mode-1] == 0)
		return -EINVAL;
	if (mode->depth != 8 && mode->depth != 16 && mode->depth != 24 && mode->depth != 32)
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
                cmode = CMODE_8;
                break;
        default:
                return -EINVAL;
        }

	if (platinum_vram_reqd(mode->mode, cmode) > total_vram)
		return -EINVAL;

	if (doit) {
		video_mode = mode->mode;
		color_mode = cmode;
		platinum_init();
	}
	return 0;
}

void
platinum_set_palette(unsigned char red[], unsigned char green[],
		     unsigned char blue[], int index, int ncolors)
{
	int i;

	for (i = 0; i < ncolors; ++i) {
		cmap_regs->addr = index + i;	eieio();
		cmap_regs->lut = red[i];	eieio();
		cmap_regs->lut = green[i];	eieio();
		cmap_regs->lut = blue[i];	eieio();
	}
}

void
platinum_set_blanking(int blank_mode)
{
}
