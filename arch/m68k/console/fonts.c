
/*
 * arch/m68k/console/fonts.c -- `Soft' font definitions
 *
 *    Created 1995 by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */


#include <linux/types.h>
#include <linux/string.h>
#include <asm/font.h>


   /*
    *    External Font Definitions
    */

/* VGA8x8 */
extern char fontname_8x8[];
extern int fontwidth_8x8, fontheight_8x8;
extern u_char fontdata_8x8[];

/* VGA8x16 */
extern char fontname_8x16[];
extern int fontwidth_8x16, fontheight_8x16;
extern u_char fontdata_8x16[];


   /*
    *    Font Descriptor Array
    */

struct softfontdesc {
   char *name;
   int *width;
   int *height;
   u_char *data;
};

static struct softfontdesc softfonts[] = {
   { fontname_8x8, &fontwidth_8x8, &fontheight_8x8, fontdata_8x8 },
   { fontname_8x16, &fontwidth_8x16, &fontheight_8x16, fontdata_8x16 }
};

static u_long numsoftfonts = sizeof(softfonts)/sizeof(*softfonts);


   /*
    *    Find a font with a specific name
    */

int findsoftfont(char *name, int *width, int *height, u_char *data[])
{
   int i;

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
                    u_char *data[])
{
   if (yres < 400) {
      if (name)
   	   *name = fontname_8x8;
   	if (width)
   	   *width = fontwidth_8x8;
   	if (height)
   	   *height = fontheight_8x8;
   	if (data)
         *data = fontdata_8x8;
	} else {
      if (name)
   	   *name = fontname_8x16;
   	if (width)
   	   *width = fontwidth_8x16;
   	if (height)
   	   *height = fontheight_8x16;
   	if (data)
         *data = fontdata_8x16;
	}
}
