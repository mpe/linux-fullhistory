/*
 * Definitions for display drivers for console use on PowerMacs.
 *
 * Copyright (C) 1997 Paul Mackerras.
 *	
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * Video mode values.
 * These are supposed to be the same as the values that
 * Apple uses in MacOS.
 */
#define VMODE_NVRAM		0	/* use value stored in nvram */
#define VMODE_512_384_60I	1	/* 512x384, 60Hz interlaced (NTSC) */
#define VMODE_512_384_60	2	/* 512x384, 60Hz */
#define VMODE_640_480_50I	3	/* 640x480, 50Hz interlaced (PAL) */
#define VMODE_640_480_60I	4	/* 640x480, 60Hz interlaced (NTSC) */
#define VMODE_640_480_60	5	/* 640x480, 60Hz (VGA) */
#define VMODE_640_480_67	6	/* 640x480, 67Hz */
#define VMODE_640_870_75P	7	/* 640x870, 75Hz (portrait) */
#define VMODE_768_576_50I	8	/* 768x576, 50Hz (PAL full frame) */
#define VMODE_800_600_56	9	/* 800x600, 56Hz */
#define VMODE_800_600_60	10	/* 800x600, 60Hz */
#define VMODE_800_600_72	11	/* 800x600, 72Hz */
#define VMODE_800_600_75	12	/* 800x600, 75Hz */
#define VMODE_832_624_75	13	/* 832x624, 75Hz */
#define VMODE_1024_768_60	14	/* 1024x768, 60Hz */
#define VMODE_1024_768_70	15	/* 1024x768, 70Hz (or 72Hz?) */
#define VMODE_1024_768_75V	16	/* 1024x768, 75Hz (VESA) */
#define VMODE_1024_768_75	17	/* 1024x768, 75Hz */
#define VMODE_1152_870_75	18	/* 1152x870, 75Hz */
#define VMODE_1280_960_75	19	/* 1280x960, 75Hz */
#define VMODE_1280_1024_75	20	/* 1280x1024, 75Hz */
#define VMODE_MAX		20
#define VMODE_CHOOSE		99	/* choose based on monitor sense */

/*
 * Color mode values, used to select number of bits/pixel.
 */
#define CMODE_NVRAM		-1	/* use value stored in nvram */
#define CMODE_8			0	/* 8 bits/pixel */
#define CMODE_16		1	/* 16 (actually 15) bits/pixel */
#define CMODE_32		2	/* 32 (actually 24) bits/pixel */

extern int video_mode;
extern int color_mode;

/*
 * Addresses in NVRAM where video mode and pixel size are stored.
 */
#define NV_VMODE	0x140f
#define NV_CMODE	0x1410

/*
 * Horizontal and vertical resolution information.
 */
extern struct vmode_attr {
	int	hres;
	int	vres;
	int	vfreq;
	int	interlaced;
} vmode_attrs[VMODE_MAX];

extern struct vc_mode display_info;

#define DEFAULT_VESA_BLANKING_MODE	VESA_NO_BLANKING

extern int pixel_size;		/* in bytes */
extern int n_scanlines;		/* # of scan lines */
extern int line_pitch;		/* # bytes in 1 scan line */
extern int row_pitch;		/* # bytes in 1 row of characters */
extern unsigned char *fb_start;	/* addr of top left pixel of top left char */

/* map monitor sense value to video mode */
extern int map_monitor_sense(int sense);

void set_palette(void);
void pmac_find_display(void);
void vesa_blank(void);
void vesa_unblank(void);
void set_vesa_blanking(const unsigned long);
void vesa_powerdown(void);
void hide_cursor(void);
void pmac_init_palette(void);
