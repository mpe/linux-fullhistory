/*
 * linux/arch/m68k/console/txtcon.c -- Low level text mode based console driver
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


#include <linux/types.h>
#include <linux/console.h>


   /*
    *    Interface used by the world
    */

static u_long txtcon_startup(u_long kmem_start, char **display_desc);
static void txtcon_init(struct vc_data *conp);
static int txtcon_deinit(struct vc_data *conp);
static int txtcon_clear(struct vc_data *conp, int sy, int sx, int height,
                        int width);
static int txtcon_putc(struct vc_data *conp, int c, int y, int x);
static int txtcon_putcs(struct vc_data *conp, const char *s, int count, int y,
                        int x);
static int txtcon_cursor(struct vc_data *conp, int mode);
static int txtcon_scroll(struct vc_data *conp, int t, int b, int dir, int count);
static int txtcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
                        int height, int width);
static int txtcon_switch(struct vc_data *conp);
static int txtcon_blank(int blank);



static u_long txtcon_startup(u_long kmem_start, char **display_desc)
{
   *display_desc = "Not yet implemented";
   return(kmem_start);
}


static void txtcon_init(struct vc_data *conp)
{
}


static int txtcon_deinit(struct vc_data *conp)
{
   return(0);
}


/* ====================================================================== */

/* txtcon_XXX routines - interface used by the world */


static int txtcon_clear(struct vc_data *conp, int sy, int sx, int height,
                        int width)
{
   return(0);
}


static int txtcon_putc(struct vc_data *conp, int c, int y, int x)
{
   return(0);
}


static int txtcon_putcs(struct vc_data *conp, const char *s, int count, int y,
                        int x)
{
   return(0);
}


static int txtcon_cursor(struct vc_data *conp, int mode)
{
   return(0);
}


static int txtcon_scroll(struct vc_data *conp, int t, int b, int dir, int count)
{
   return(0);
}


static int txtcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
                        int height, int width)
{
   return(0);
}


static int txtcon_switch(struct vc_data *conp)
{
   return(0);
}


static int txtcon_blank(int blank)
{
   return(0);
}


/* ====================================================================== */

   /*
    *    The console `switch' structure for the text mode based console
    */

struct consw txt_con = {
   txtcon_startup, txtcon_init, txtcon_deinit, txtcon_clear, txtcon_putc,
   txtcon_putcs, txtcon_cursor, txtcon_scroll, txtcon_bmove, txtcon_switch,
   txtcon_blank
};

