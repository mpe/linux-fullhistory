/* $Id: promfb.h,v 1.1 1998/07/05 22:50:43 ecd Exp $
 *
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _PROMFB_H
#define _PROMFB_H 1

#include <asm/oplib.h>

#include "fbcon.h"

struct fb_info_promfb {
	struct fb_info info;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	struct display disp;
	struct display_switch dispsw;
	int node;
	unsigned short *data;
	int curx, cury;
	int maxx, maxy;
};

#define promfbinfo(info)	((struct fb_info_promfb *)(info))
#define promfbinfod(disp)	((struct fb_info_promfb *)(disp->fb_info))

#endif /* !(_PROM_FB_H) */
