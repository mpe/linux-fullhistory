/*
 *  linux/drivers/video/macmodes.c -- Standard MacOS video modes
 *
 *	Copyright (C) 1998 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/string.h>

#include <video/macmodes.h>

struct mac_mode {
    int number;
    u32 xres;
    u32 yres;
    u32 pixclock;
    u32 left_margin;
    u32 right_margin;
    u32 upper_margin;
    u32 lower_margin;
    u32 hsync_len;
    u32 vsync_len;
    u32 sync;
    u32 vmode;
};


    /* 512x384, 60Hz, Interlaced (NTSC) */

#if 0
static const struct mac_mode mac_mode_1 = {
    VMODE_512_384_60I, 512, 384,
    pixclock, left, right, upper, lower, hslen, vslen,
    sync, FB_VMODE_INTERLACED
};
#endif

    /* 512x384, 60Hz, Non-Interlaced */

#if 0
static const struct mac_mode mac_mode_2 = {
    VMODE_512_384_60, 512, 384,
    pixclock, left, right, upper, lower, hslen, vslen,
    sync, FB_VMODE_NONINTERLACED
};
#endif

    /* 640x480, 50Hz, Interlaced (PAL) */

#if 0
static const struct mac_mode mac_mode_3 = {
    VMODE_640_480_50I, 640, 480,
    pixclock, left, right, upper, lower, hslen, vslen,
    sync, FB_VMODE_INTERLACED
};
#endif

    /* 640x480, 60Hz, Interlaced (NTSC) */

#if 0
static const struct mac_mode mac_mode_4 = {
    VMODE_640_480_60I, 640, 480,
    pixclock, left, right, upper, lower, hslen, vslen,
    sync, FB_VMODE_INTERLACED
};
#endif

    /* 640x480, 60 Hz, Non-Interlaced (25.175 MHz dotclock) */

static const struct mac_mode mac_mode_5 = {
    VMODE_640_480_60, 640, 480,
    39722, 32, 32, 33, 10, 96, 2,
    0, FB_VMODE_NONINTERLACED
};

    /* 640x480, 67Hz, Non-Interlaced (30.0 MHz dotclock) */

static const struct mac_mode mac_mode_6 = {
    VMODE_640_480_67, 640, 480,
    33334, 80, 80, 39, 3, 64, 3,
    0, FB_VMODE_NONINTERLACED
};

    /* 640x870, 75Hz (portrait), Non-Interlaced */

#if 0
static const struct mac_mode mac_mode_7 = {
    VMODE_640_870_75P, 640, 870,
    pixclock, left, right, upper, lower, hslen, vslen,
    sync, FB_VMODE_NONINTERLACED
};
#endif

    /* 768x576, 50Hz (PAL full frame), Interlaced */

#if 0
static const struct mac_mode mac_mode_8 = {
    VMODE_768_576_50I, 768, 576,
    pixclock, left, right, upper, lower, hslen, vslen,
    sync, FB_VMODE_INTERLACED
};
#endif

    /* 800x600, 56 Hz, Non-Interlaced (36.00 MHz dotclock) */

static const struct mac_mode mac_mode_9 = {
    VMODE_800_600_56, 800, 600,
    27778, 112, 40, 22, 1, 72, 2,
    FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};

    /* 800x600, 60 Hz, Non-Interlaced (40.00 MHz dotclock) */

static const struct mac_mode mac_mode_10 = {
    VMODE_800_600_60, 800, 600,
    25000, 72, 56, 23, 1, 128, 4,
    FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};

    /* 800x600, 72 Hz, Non-Interlaced (50.00 MHz dotclock) */

static const struct mac_mode mac_mode_11 = {
    VMODE_800_600_72, 800, 600,
    20000, 48, 72, 23, 37, 120, 6,
    FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};

    /* 800x600, 75 Hz, Non-Interlaced (49.50 MHz dotclock) */

static const struct mac_mode mac_mode_12 = {
    VMODE_800_600_75, 800, 600,
    20203, 144, 32, 21, 1, 80, 3,
    FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};

    /* 832x624, 75Hz, Non-Interlaced (57.6 MHz */

static const struct mac_mode mac_mode_13 = {
    VMODE_832_624_75, 832, 624,
    17362, 208, 48, 39, 1, 64, 3,
    0, FB_VMODE_NONINTERLACED
};

    /* 1024x768, 60 Hz, Non-Interlaced (65.00 MHz dotclock) */

static const struct mac_mode mac_mode_14 = {
    VMODE_1024_768_60, 1024, 768,
    15385, 144, 40, 29, 3, 136, 6,
    0, FB_VMODE_NONINTERLACED
};

    /* 1024x768, 72 Hz, Non-Interlaced (75.00 MHz dotclock) */

static const struct mac_mode mac_mode_15 = {
    VMODE_1024_768_70, 1024, 768,
    13334, 128, 40, 29, 3, 136, 6,
    0, FB_VMODE_NONINTERLACED
};

    /* 1024x768, 75 Hz, Non-Interlaced (78.75 MHz dotclock) */

static const struct mac_mode mac_mode_16 = {
    VMODE_1024_768_75V, 1024, 768,
    12699, 176, 16, 28, 1, 96, 3,
    FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};

    /* 1024x768, 75 Hz, Non-Interlaced (78.75 MHz dotclock) */

static const struct mac_mode mac_mode_17 = {
    VMODE_1024_768_75, 1024, 768,
    12699, 160, 32, 28, 1, 96, 3,
    FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};

    /* 1152x870, 75 Hz, Non-Interlaced (100.0 MHz dotclock) */

static const struct mac_mode mac_mode_18 = {
    VMODE_1152_870_75, 1152, 870,
    10000, 128, 48, 39, 3, 128, 3,
    FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};

    /* 1280x960, 75 Hz, Non-Interlaced (126.00 MHz dotclock) */

static const struct mac_mode mac_mode_19 = {
    VMODE_1280_960_75, 1280, 960,
    7937, 224, 32, 36, 1, 144, 3,
    0, FB_VMODE_NONINTERLACED
};

    /* 1280x1024, 75 Hz, Non-Interlaced (135.00 MHz dotclock) */

static const struct mac_mode mac_mode_20 = {
    VMODE_1280_1024_75, 1280, 1024,
    7408, 232, 64, 38, 1, 112, 3,
    FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};


static const struct mac_mode *mac_modes[20] = {
    NULL,		/* 512x384, 60Hz interlaced (NTSC) */
    NULL,		/* 512x384, 60Hz */
    NULL,		/* 640x480, 50Hz interlaced (PAL) */
    NULL,		/* 640x480, 60Hz interlaced (NTSC) */
    &mac_mode_5,	/* 640x480, 60Hz (VGA) */
    &mac_mode_6,	/* 640x480, 67Hz */
    NULL,		/* 640x870, 75Hz (portrait) */
    NULL,		/* 768x576, 50Hz (PAL full frame) */
    &mac_mode_9,	/* 800x600, 56Hz */
    &mac_mode_10,	/* 800x600, 60Hz */
    &mac_mode_11,	/* 800x600, 72Hz */
    &mac_mode_12,	/* 800x600, 75Hz */
    &mac_mode_13,	/* 832x624, 75Hz */
    &mac_mode_14,	/* 1024x768, 60Hz */
    &mac_mode_15,	/* 1024x768, 70Hz (or 72Hz?) */
    &mac_mode_16,	/* 1024x768, 75Hz (VESA) */
    &mac_mode_17,	/* 1024x768, 75Hz */
    &mac_mode_18,	/* 1152x870, 75Hz */
    &mac_mode_19,	/* 1280x960, 75Hz */
    &mac_mode_20,	/* 1280x1024, 75Hz */
};

static const struct mac_mode *mac_modes_inv[] = {
    &mac_mode_6,	/* 640x480, 67Hz */
    &mac_mode_5,	/* 640x480, 60Hz (VGA) */
    &mac_mode_12,	/* 800x600, 75Hz */
    &mac_mode_11,	/* 800x600, 72Hz */
    &mac_mode_10,	/* 800x600, 60Hz */
    &mac_mode_9,	/* 800x600, 56Hz */
    &mac_mode_13,	/* 832x624, 75Hz */
    &mac_mode_17,	/* 1024x768, 75Hz */
    &mac_mode_16,	/* 1024x768, 75Hz (VESA) */
    &mac_mode_15,	/* 1024x768, 70Hz (or 72Hz?) */
    &mac_mode_14,	/* 1024x768, 60Hz */
    &mac_mode_18,	/* 1152x870, 75Hz */
    &mac_mode_19,	/* 1280x960, 75Hz */
    &mac_mode_20,	/* 1280x1024, 75Hz */
};


static struct mon_map {
    int sense;
    int vmode;
} monitor_map[] = {
    { 0x000, VMODE_1280_1024_75 },	/* 21" RGB */
    { 0x114, VMODE_640_870_75P },	/* Portrait Monochrome */
    { 0x221, VMODE_512_384_60 },	/* 12" RGB*/
    { 0x331, VMODE_1280_1024_75 },	/* 21" RGB (Radius) */
    { 0x334, VMODE_1280_1024_75 },	/* 21" mono (Radius) */
    { 0x335, VMODE_1280_1024_75 },	/* 21" mono */
    { 0x40A, VMODE_640_480_60I },	/* NTSC */
    { 0x51E, VMODE_640_870_75P },	/* Portrait RGB */
    { 0x603, VMODE_832_624_75 },	/* 12"-16" multiscan */
    { 0x60b, VMODE_1024_768_70 },	/* 13"-19" multiscan */
    { 0x623, VMODE_1152_870_75 },	/* 13"-21" multiscan */
    { 0x62b, VMODE_640_480_67 },	/* 13"/14" RGB */
    { 0x700, VMODE_640_480_50I },	/* PAL */
    { 0x714, VMODE_640_480_60I },	/* NTSC */
    { 0x717, VMODE_800_600_75 },	/* VGA */
    { 0x72d, VMODE_832_624_75 },	/* 16" RGB (Goldfish) */
    { 0x730, VMODE_768_576_50I },	/* PAL (Alternate) */
    { 0x73a, VMODE_1152_870_75 },	/* 3rd party 19" */
    { 0x73f, VMODE_640_480_67 },	/* no sense lines connected at all */
    { -1,    VMODE_640_480_60 },	/* catch-all, must be last */
};


    /*
     *  Convert a MacOS vmode/cmode pair to a frame buffer video mode structure
     */

int mac_vmode_to_var(int vmode, int cmode, struct fb_var_screeninfo *var)
{
    const struct mac_mode *mode = NULL;

    if (vmode > 0 && vmode <= VMODE_MAX)
	mode = mac_modes[vmode-1];

    if (!mode)
	return -EINVAL;

    memset(var, 0, sizeof(struct fb_var_screeninfo));
    switch (cmode) {
	case CMODE_8:
	    var->bits_per_pixel = 8;
	    var->red.offset = 0;
	    var->red.length = 8;   
	    var->green.offset = 0;
	    var->green.length = 8;
	    var->blue.offset = 0;
	    var->blue.length = 8;
	    break;

	case CMODE_16:
	    var->bits_per_pixel = 16;
	    var->red.offset = 10;
	    var->red.length = 5;
	    var->green.offset = 5;
	    var->green.length = 5;
	    var->blue.offset = 0;
	    var->blue.length = 5;
	    break;

	case CMODE_32:
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

	default:
	    return -EINVAL;
    }
    var->xres = mode->xres;
    var->yres = mode->yres;
    var->xres_virtual = mode->xres;
    var->yres_virtual = mode->yres;
    var->height = -1;
    var->width = -1;
    var->pixclock = mode->pixclock;
    var->left_margin = mode->left_margin;
    var->right_margin = mode->right_margin;
    var->upper_margin = mode->upper_margin;
    var->lower_margin = mode->lower_margin;
    var->hsync_len = mode->hsync_len;
    var->vsync_len = mode->vsync_len;
    var->sync = mode->sync;
    var->vmode = mode->vmode;
    return 0;
}


    /*
     *  Convert a frame buffer video mode structure to a MacOS vmode/cmode pair
     */

int mac_var_to_vmode(const struct fb_var_screeninfo *var, int *vmode,
		     int *cmode)
{
    unsigned int i;

    if (var->bits_per_pixel <= 8)
	*cmode = CMODE_8;
    else if (var->bits_per_pixel <= 16)
	*cmode = CMODE_16;
    else if (var->bits_per_pixel <= 32)
	*cmode = CMODE_32;
    else
	return -EINVAL;

    for (i = 0; i < sizeof(mac_modes_inv)/sizeof(*mac_modes_inv); i++) {
	const struct mac_mode *mode = mac_modes_inv[i];
	if (var->xres > mode->xres || var->yres > mode->yres)
	    continue;
	if (var->xres_virtual > mode->xres || var->yres_virtual > mode->yres)
	    continue;
	if (var->pixclock > mode->pixclock)
	    continue;
	if ((var->vmode & FB_VMODE_MASK) != mode->vmode)
	    continue;
	*vmode = mode->number;
	return 0;
    }
    return -EINVAL;
}


    /*
     *  Convert a Mac monitor sense number to a MacOS vmode number
     */

int mac_map_monitor_sense(int sense)
{
    struct mon_map *map;

    for (map = monitor_map; map->sense >= 0; ++map)
	if (map->sense == sense)
	    break;
    return map->vmode;
}
