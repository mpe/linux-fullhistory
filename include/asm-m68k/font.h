/*
 *  asm-m68k/font.h -- `Soft' font definitions
 *
 *  Created 1995 by Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _ASM_M68K_FONT_H_
#define _ASM_M68K_FONT_H_

#include <linux/types.h>


   /*
    *    Find a font with a specific name
    */

extern int findsoftfont(char *name, int *width, int *height, u_char *data[]);


   /*
    *    Get the default font for a specific screen size
    */

extern void getdefaultfont(int xres, int yres, char *name[], int *width,
                           int *height, u_char *data[]);


/* Max. length for the name of a predefined font */
#define MAX_FONT_NAME	32

#endif /* _ASM_M68K_FONT_H_ */
