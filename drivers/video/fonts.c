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
#ifdef __mc68000__
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


   /*
    *    Font Descriptor Array
    */

struct softfontdesc {
   char *name;
   int *width;
   int *height;
   u8 *data;
};

#define VGA8x8_IDX	0
#define VGA8x16_IDX	1
#define PEARL8x8_IDX	2
#define VGA6x11_IDX	3

static struct softfontdesc softfonts[] = {
   { fontname_8x8, &fontwidth_8x8, &fontheight_8x8, fontdata_8x8 },
   { fontname_8x16, &fontwidth_8x16, &fontheight_8x16, fontdata_8x16 },
   { fontname_pearl8x8, &fontwidth_pearl8x8, &fontheight_pearl8x8,
     fontdata_pearl8x8 },
   { fontname_6x11, &fontwidth_6x11, &fontheight_6x11, fontdata_6x11 },
};

static unsigned int numsoftfonts = sizeof(softfonts)/sizeof(*softfonts);


   /*
    *    Find a font with a specific name
    */

int findsoftfont(char *name, int *width, int *height, u8 *data[])
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

void getdefaultfont(int xres, int yres, char *name[], int *width, int *height,
                    u8 *data[])
{
    int i;
    
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

    if (name)
	*name = softfonts[i].name;
    if (width)
	*width = *softfonts[i].width;
    if (height)
	*height = *softfonts[i].height;
    if (data)
	*data = softfonts[i].data;
}
