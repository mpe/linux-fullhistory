/*
 * include/asm-ppc/linux_logo.h: A linux logo to be displayed on boot
 * (pinched from the sparc port).
 *
 * Copyright (C) 1996 Larry Ewing (lewing@isc.tamu.edu)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * You can put anything here, but:
 * LINUX_LOGO_COLORS has to be less than 224
 * values have to start from 0x20
 * (i.e. linux_logo_{red,green,blue}[0] is color 0x20)
 */
 
#include <linux/init.h>

#define linux_logo_banner "Linux/PPC version " UTS_RELEASE

#define LINUX_LOGO_HEIGHT	80
#define LINUX_LOGO_WIDTH	80
#define LINUX_LOGO_COLORS	221

#ifdef INCLUDE_LINUX_LOGO_DATA

#define INCLUDE_LINUX_LOGO16
#include <linux/linux_logo.h>

/* Painted by Johnny Stenback <jst@uwasa.fi> */

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
"   ddd |  PPC   $9$F\n"
" '!!!!!$       !!#!`\n"
"  !!!!!*     .!!!!!`\n"
"'!!!!!!!W..e$$!!!!!!`    %s\n"
" \"~^^~         ^~~^\n"
"\n";

#else

/* prototypes only */
extern unsigned char linux_logo_red[];
extern unsigned char linux_logo_green[];
extern unsigned char linux_logo_blue[];
extern unsigned char linux_logo[];
extern unsigned char linux_logo_bw[];
extern unsigned char linux_logo16_red[];
extern unsigned char linux_logo16_green[];
extern unsigned char linux_logo16_blue[];
extern unsigned char linux_logo16[];
extern unsigned char *linux_serial_image;

extern int (*console_show_logo)(void);

#endif
