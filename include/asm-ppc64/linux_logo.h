/*
 * include/asm-ppc64/linux_logo.h: A linux logo to be displayed on boot
 * (pinched from the sparc port).
 *
 * Copyright (C) 1996 Larry Ewing (lewing@isc.tamu.edu)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * You can put anything here, but:
 * LINUX_LOGO_COLORS has to be less than 224
 * values have to start from 0x20
 * (i.e. linux_logo_{red,green,blue}[0] is color 0x20)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
 
#include <linux/init.h>

#define linux_logo_banner "Linux/PPC-64 version " UTS_RELEASE

#define LINUX_LOGO_HEIGHT	80
#define LINUX_LOGO_WIDTH	80

#include <linux/linux_logo.h>
