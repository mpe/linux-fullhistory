/*
 * linux/drivers/video/txtcon.c -- Low level text mode based console driver
 *
 *    Copyright (C) 1995 Geert Uytterhoeven
 *
 *
 * This file is currently only a skeleton, since all Amigas and Ataris have
 * bitmapped graphics.
 *
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */


#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/init.h>


    /*
     *  Interface used by the world
     */

static unsigned long txtcon_startup(unsigned long kmem_start,
				    const char **display_desc);
static void txtcon_init(struct vc_data *conp);
static void txtcon_deinit(struct vc_data *conp);
static void txtcon_clear(struct vc_data *conp, int sy, int sx, int height,
			 int width);
static void txtcon_putc(struct vc_data *conp, int c, int y, int x);
static void txtcon_putcs(struct vc_data *conp, const char *s, int count, int y,
			 int x);
static void txtcon_cursor(struct vc_data *conp, int mode);
static void txtcon_scroll(struct vc_data *conp, int t, int b, int dir,
			  int count);
static void txtcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
			 int height, int width);
static int txtcon_switch(struct vc_data *conp);
static int txtcon_blank(int blank);
static int txtcon_get_font(struct vc_data *conp, int *w, int *h, char *data);
static int txtcon_set_font(struct vc_data *conp, int w, int h, char *data);
static int txtcon_set_palette(struct vc_data *conp, unsigned char *table);
static int txtcon_scrolldelta(struct vc_data *conp, int lines);


static unsigned long txtcon_startup(unsigned long kmem_start,
				    const char **display_desc)
{
    return kmem_start;
}


static void txtcon_init(struct vc_data *conp)
{
    /* ... */
}


static void txtcon_deinit(struct vc_data *conp)
{
    /* ... */
}


/* ====================================================================== */

/* txtcon_XXX routines - interface used by the world */


static void txtcon_clear(struct vc_data *conp, int sy, int sx, int height,
			 int width)
{
    /* ... */
}


static void txtcon_putc(struct vc_data *conp, int c, int y, int x)
{
    /* ... */
}


static void txtcon_putcs(struct vc_data *conp, const char *s, int count, int y,
			 int x)
{
    /* ... */
}


static void txtcon_cursor(struct vc_data *conp, int mode)
{
    /* ... */
}


static void txtcon_scroll(struct vc_data *conp, int t, int b, int dir,
			  int count)
{
    /* ... */
}


static void txtcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
			 int height, int width)
{
    /* ... */
}


static int txtcon_switch(struct vc_data *conp)
{
    return -ENOSYS;
}


static int txtcon_blank(int blank)
{
    return -ENOSYS;
}


static int txtcon_get_font(struct vc_data *conp, int *w, int *h, char *data)
{
    return -ENOSYS;
}


static int txtcon_set_font(struct vc_data *conp, int w, int h, char *data)
{
    return -ENOSYS;
}


static int txtcon_set_palette(struct vc_data *conp, unsigned char *table)
{
    return -ENOSYS;
}


static int txtcon_scrolldelta(struct vc_data *conp, int lines)
{
    return -ENOSYS;
}


/* ====================================================================== */

    /*
     *  The console `switch' structure for the text mode based console
     */

struct consw txt_con = {
    txtcon_startup,
    txtcon_init,
    txtcon_deinit,
    txtcon_clear,
    txtcon_putc,
    txtcon_putcs,
    txtcon_cursor,
    txtcon_scroll,
    txtcon_bmove,
    txtcon_switch,
    txtcon_blank,
    txtcon_get_font,
    txtcon_set_font,
    txtcon_set_palette,
    txtcon_scrolldelta
};
