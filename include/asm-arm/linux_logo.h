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

/* Painted by Johnny Stenback <jst@uwasa.fi>
 * Modified by Russell King for ARM
 */
unsigned char *linux_serial_image __initdata = "\n"
"         .u$e.\n"
"       .$$$$$:S\n"
"       $\"*$/\"*$$\n"
"       $.`$ . ^F\n"
"       4k+#+T.$F\n"
"       4P+++\"$\"$\n"
"       :R\"+  t$$B\n"
"    ___#       $$$\n"
"    |  |       R$$k\n"
"   dd. | Linux  $!$\n"
"   ddd |   ARM  $9$F\n"
" '!!!!!$       !!#!`\n"
"  !!!!!*     .!!!!!`\n"
"'!!!!!!!W..e$$!!!!!!`    %s\n"
" \"~^^~         ^~~^\n"
"\n";

extern int (*console_show_logo)(void);
