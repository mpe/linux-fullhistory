/*
 * linux/include/asm-arm/linux_logo.h
 *
 * Copyright (C) 1998 Russell King
 *
 * Linux console driver logo definitions for ARM
 */

#include <linux/init.h>
#include <linux/version.h>

#define linux_logo_banner "ARM Linux version " UTS_RELEASE

#define LINUX_LOGO_COLORS	0

unsigned char linux_logo_red[] __initdata = { };
unsigned char linux_logo_green[] __initdata = { };
unsigned char linux_logo_blue[] __initdata = { };

unsigned char linux_logo16_red[] __initdata = { };
unsigned char linux_logo16_green[] __initdata = { };
unsigned char linux_logo16_blue[] __initdata = { };

unsigned char linux_logo[] __initdata = { };
unsigned char linux_logo16[] __initdata = { };
unsigned char linux_logo_bw[] __initdata = { };

