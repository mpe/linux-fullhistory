/* $Id: linux_logo.h,v 1.2 1998/05/04 14:20:59 jj Exp $
 * include/asm-sparc/linux_logo.h: This is a linux logo
 *                                 to be displayed on boot.
 *
 * Copyright (C) 1996 Larry Ewing (lewing@isc.tamu.edu)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * You can put anything here, but:
 * LINUX_LOGO_COLORS has to be less than 224
 * image size has to be 80x80
 * values have to start from 0x20
 * (i.e. RGB(linux_logo_red[0],
 *	     linux_logo_green[0],
 *	     linux_logo_blue[0]) is color 0x20)
 * BW image has to be 80x80 as well, with MS bit
 * on the left
 * Serial_console ascii image can be any size,
 * but should contain %s to display the version
 */
 
#include <linux/init.h>
#include <linux/version.h>

#define linux_logo_banner "Linux/SPARC version " UTS_RELEASE

#define LINUX_LOGO_COLORS 221

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
"   ddd |  Sparc $9$F\n"
" '!!!!!$       !!#!`\n"
"  !!!!!*     .!!!!!`\n"
"'!!!!!!!W..e$$!!!!!!`    %s\n"
" \"~^^~         ^~~^\n"
"\n";
