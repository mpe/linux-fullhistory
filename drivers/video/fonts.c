/*
 * linux/drivers/video/fonts.c -- `Soft' font definitions
 *
 *    Created 1995 by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */


#include <linux/config.h>
#include <linux/types.h>
#include <linux/string.h>
#if defined(__mc68000__) || defined(CONFIG_APUS)
#include <asm/setup.h>
#endif
#include "font.h"


   /*
    *    External Font Definitions
    */

/* VGA8x8 */
extern char fontname_8x8[];
extern int fontwidth_8x8, fontheight_8x8;
extern u8 fontdata_8x8[];

/* VGA8x16 */
extern char fontname_8x16[];
extern int fontwidth_8x16, fontheight_8x16;
extern u8 fontdata_8x16[];

/* PEARL8x8 */
extern char fontname_pearl8x8[];
extern int fontwidth_pearl8x8, fontheight_pearl8x8;
extern u8 fontdata_pearl8x8[];

/* VGA6x11 */
extern char fontname_6x11[];
extern int fontwidth_6x11, fontheight_6x11;
extern u8 fontdata_6x11[];

/* SUN8x16 */
extern char fontname_sun8x16[];
extern int fontwidth_sun8x16, fontheight_sun8x16;
extern u8 fontdata_sun8x16[];

/* SUN12x22 */
extern char fontname_sun12x22[];
extern int fontwidth_sun12x22, fontheight_sun12x22;
extern u8 fontdata_sun12x22[];



   /*
    *    Font Descriptor Array
    */

struct softfontdesc {
   int idx;
   char *name;
   int *width;
   int *height;
   u8 *data;
};

#define VGA8x8_IDX	0
#define VGA8x16_IDX	1
#define PEARL8x8_IDX	2
#define VGA6x11_IDX	3
#define SUN8x16_IDX	4
#define SUN12x22_IDX	5

static struct softfontdesc softfonts[] = {
   { VGA8x8_IDX, fontname_8x8, &fontwidth_8x8, &fontheight_8x8, fontdata_8x8 },
#ifndef __sparc__
   { VGA8x16_IDX, fontname_8x16, &fontwidth_8x16, &fontheight_8x16, fontdata_8x16 },
   { PEARL8x8_IDX, fontname_pearl8x8, &fontwidth_pearl8x8, &fontheight_pearl8x8,
     fontdata_pearl8x8 },
   { VGA6x11_IDX, fontname_6x11, &fontwidth_6x11, &fontheight_6x11, fontdata_6x11 },
#else
   { SUN8x16_IDX, fontname_sun8x16, &fontwidth_sun8x16, &fontheight_sun8x16, 
     fontdata_sun8x16 },
   { SUN12x22_IDX, fontname_sun12x22, &fontwidth_sun12x22, &fontheight_sun12x22, 
     fontdata_sun12x22 },
#endif
};

static unsigned int numsoftfonts = sizeof(softfonts)/sizeof(*softfonts);


   /*
    *    Find a font with a specific name
    */

int findsoftfont(char *name, unsigned short *width, unsigned short *height, u8 *data[])
{
   unsigned int i;

   for (i = 0; i < numsoftfonts; i++)
      if (!strcmp(softfonts[i].name, name)) {
         if (width)
            *width = *softfonts[i].width;
         if (height)
            *height = *softfonts[i].height;
         if (data)
            *data = softfonts[i].data;
			return(1);
      }
	return(0);
}


   /*
    *    Get the default font for a specific screen size
    */

void getdefaultfont(int xres, int yres, char *name[], unsigned short *width, unsigned short *height,
                    u8 *data[])
{
    int i, j;
    
    if (yres < 400) {
	i = VGA8x8_IDX;
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA)
	    i = PEARL8x8_IDX;
#endif
    } else
	i = VGA8x16_IDX;

#if defined(CONFIG_MAC)
    if (MACH_IS_MAC) {
#if 0  /* MSch: removed until 6x11 is debugged */
        i = VGA6x11_IDX;   /* I added this for fun ... I like 6x11 */
#endif
       if (xres < 640)
           i = VGA6x11_IDX;
    }
#endif

#ifdef __sparc__
    i = SUN8x16_IDX;
#endif

    for (j = 0; j < numsoftfonts; j++)
        if (softfonts[j].idx == i)
            break;

    if (name)
	*name = softfonts[j].name;
    if (width)
	*width = *softfonts[j].width;
    if (height)
	*height = *softfonts[j].height;
    if (data)
	*data = softfonts[j].data;
}
