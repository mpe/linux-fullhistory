/*
 * chips.c: Console support for PowerBook 3400/2400 chips65550 display adaptor.
 *
 * Copyright (C) 1997 Fabio Riccardi.
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
#include <linux/selection.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/adb.h>
#include <asm/cuda.h>
#include <asm/pmu.h>
#include <asm/pci-bridge.h>
#include "pmac-cons.h"
#include "chips.h"


static unsigned char *frame_buffer;
static unsigned char *blitter_regs;
static unsigned char *io_space;
static unsigned long chips_base_phys;
static unsigned long chips_io_phys;

void
map_chips_display(struct device_node *dp)
{
    unsigned char bus, devfn;
    unsigned short cmd;
    unsigned long addr;

    addr = dp->addrs[0].address;
    chips_base_phys = addr;
    frame_buffer = __ioremap(addr + 0x800000, 0x100000, _PAGE_WRITETHRU);
    blitter_regs = ioremap(addr + 0xC00000, 4096);
    
    printk("Mapped chips65550 frame buffer at %p, blitter at %p\n",
	   frame_buffer, blitter_regs);
    
    if (pci_device_loc(dp, &bus, &devfn) == 0) {
	pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd);
	cmd |= 3;	// enable memory and IO space
	pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
	io_space = (unsigned char *) pci_io_base(bus);
	/* XXX really want the physical address here */
	chips_io_phys = (unsigned long) pci_io_base(bus);
	printk("Chips65550 IO space at %p\n", io_space);
    }

    video_mode = VMODE_800_600_60;
    color_mode = CMODE_8;
}

#define write_xr(num,val)	{ out_8(io_space + 0x3D6, num); out_8(io_space + 0x3D7, val); }
#define read_xr(num,var)	{ out_8(io_space + 0x3D6, num); var = in_8(io_space + 0x3D7); }
#define write_fr(num,val)	{ out_8(io_space + 0x3D0, num); out_8(io_space + 0x3D1, val); }
#define read_fr(num,var)	{ out_8(io_space + 0x3D0, num); var = in_8(io_space + 0x3D1); }
#define write_cr(num,val)	{ out_8(io_space + 0x3D4, num); out_8(io_space + 0x3D5, val); }
#define read_cr(num,var)	{ out_8(io_space + 0x3D4, num); var = in_8(io_space + 0x3D5); }

void
chips_init()
{
    unsigned *p;
    int i, hres;

    if (video_mode != VMODE_800_600_60) {
	printk(KERN_ERR "chips65550: display mode %d not supported", video_mode);
	video_mode = VMODE_800_600_60;
    }

    if (color_mode != CMODE_8 && color_mode != CMODE_16) {
	printk(KERN_ERR "chips65550: color mode %d not supported", color_mode);
	color_mode = CMODE_8;
    }
    
    n_scanlines = 600;
    hres        = 800;
    pixel_size  = 1 << color_mode;
    line_pitch  = hres * pixel_size;
    row_pitch = line_pitch * 16;

    if (color_mode == CMODE_16) {
	write_cr(0x13, 200);		// 16 bit display width (decimal)
	write_xr(0x81, 0x14);		// 15 bit (TrueColor) color mode
	write_xr(0x82, 0x00);		// disable palettes
	write_xr(0x20, 0x10);		// 16 bit blitter mode
	// write_xr(0x80, 0x00);	// 6 bit DAC
	// write_fr(0x11, 0X50);	// No dither, 5 bits/color
    } else if (color_mode == CMODE_8) {
	write_cr(0x13, 100);		// 8 bit display width (decimal)
	write_xr(0x81, 0x12);		// 8 bit color mode
	write_xr(0x82, 0x08);		// Graphics gamma enable
	write_xr(0x20, 0x00);		// 8 bit blitter mode
	// write_xr(0x80, 0x82);	// 8 bit DAC, CRT overscan
	// write_fr(0x11, 0XE0);	// Res Dither on, 6 bits/pixel
    }

    pmac_init_palette();	/* Initialize colormap */

    fb_start = frame_buffer;

    printk(KERN_INFO "hres = %d height = %d pitch = %d\n",
	   hres, n_scanlines, line_pitch);

    display_info.height = n_scanlines;
    display_info.width = hres;
    display_info.depth = pixel_size * 8;
    display_info.pitch = line_pitch;
    display_info.mode = video_mode;
    strncpy(display_info.name, "chips65550", sizeof(display_info.name));
    display_info.fb_address = chips_base_phys + 0x800000;
    display_info.cmap_adr_address = chips_io_phys + 0x3c8;
    display_info.cmap_data_address = chips_io_phys + 0x3c9;
    display_info.disp_reg_address = chips_base_phys + 0xC00000;

    /* Clear screen */
    p = (unsigned *) frame_buffer;
    for (i = n_scanlines * line_pitch / sizeof(unsigned); i != 0; --i)
	*p++ = 0;

    /* Turn on backlight */
    pmu_enable_backlight(1);
}

int
chips_setmode(struct vc_mode *mode, int doit)
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

    if (mode->mode != VMODE_800_600_60)
	return -EINVAL;
    
    if (doit) {
	video_mode = mode->mode;
	color_mode = cmode;
	chips_init();
    }

    return 0;
}

void
chips_set_palette(unsigned char red[], unsigned char green[],
		  unsigned char blue[], int index, int ncolors)
{
    int i;

    for (i = 0; i < ncolors; ++i) {
	out_8(&io_space[0x3C8], index + i);
	udelay(1);
	out_8(&io_space[0x3C9], red[i]);
	out_8(&io_space[0x3C9], green[i]);
	out_8(&io_space[0x3C9], blue[i]);
    }
}

void
chips_set_blanking(int blank_mode)
{
    pmu_enable_backlight(blank_mode == VESA_NO_BLANKING);
}
