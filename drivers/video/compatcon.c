/*
 *  linux/drivers/video/compatcon.c -- Console wrapper
 *
 *	Created 26 Apr 1998 by Geert Uytterhoeven
 *
 *  This will be removed once there are frame buffer devices for all supported
 *  graphics hardware
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */


#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/linux_logo.h>

#include <linux/console_compat.h>


    /*
     *  Interface used by the world
     */

static const char *compatcon_startup(void);
static void compatcon_init(struct vc_data *conp);
static void compatcon_deinit(struct vc_data *conp);
static void compatcon_clear(struct vc_data *conp, int sy, int sx, int height,
			    int width);
static void compatcon_putc(struct vc_data *conp, int c, int ypos, int xpos);
static void compatcon_putcs(struct vc_data *conp, const unsigned short *s, int count,
			    int ypos, int xpos);
static void compatcon_cursor(struct vc_data *conp, int mode);
static int compatcon_scroll(struct vc_data *conp, int t, int b, int dir,
			    int count);
static void compatcon_bmove(struct vc_data *conp, int sy, int sx, int dy,
			    int dx, int height, int width);
static int compatcon_switch(struct vc_data *conp);
static int compatcon_blank(int blank);
static int compatcon_get_font(struct vc_data *conp, int *w, int *h, char *data);
static int compatcon_set_font(struct vc_data *conp, int w, int h, char *data);
static int compatcon_set_palette(struct vc_data *conp, unsigned char *table);
static int compatcon_scrolldelta(struct vc_data *conp, int lines);


    /*
     *  Internal routines
     */

unsigned long video_num_columns;
unsigned long video_num_lines;
unsigned long video_size_row;
unsigned char video_type;
unsigned long video_mem_base;
unsigned long video_mem_term;
unsigned long video_screen_size;
int can_do_color;

extern void set_cursor(int currcons);
extern void hide_cursor(void);
extern int set_get_cmap(unsigned char *arg, int set);
extern void set_palette(void);
extern int set_get_font(unsigned char *arg, int set, int ch512);
extern void set_vesa_blanking(unsigned long arg);
extern void vesa_blank(void);
extern void vesa_powerdown(void);
extern int con_adjust_height(unsigned long fontheight);
extern void con_type_init(const char **);
extern void con_type_init_finish(void);

#define BLANK	0x0020


__initfunc(static const char *compatcon_startup(void))
{
    const char *display_desc = "????";

    video_num_lines = ORIG_VIDEO_LINES;
    video_num_columns = ORIG_VIDEO_COLS;
    video_size_row = 2*video_num_columns;
    video_screen_size = video_num_lines*video_size_row;

    con_type_init(&display_desc);
    return display_desc;
}


static void compatcon_init(struct vc_data *conp)
{
    static int first = 1;

    conp->vc_cols = video_num_columns;
    conp->vc_rows = video_num_lines;
    conp->vc_can_do_color = can_do_color;
    if (first) {
	con_type_init_finish();
	first = 0;
    }
}

static void compatcon_deinit(struct vc_data *conp)
{
}


static void compatcon_clear(struct vc_data *conp, int sy, int sx, int height,
			    int width)
{
    int rows;
    unsigned long dest;

    if (console_blanked)
	return;

    dest = video_mem_base+sy*video_size_row+sx*2;
    if (sx == 0 && width == video_num_columns)      
	memsetw((void *)dest, conp->vc_video_erase_char, height*video_size_row);
    else
	for (rows = height; rows-- ; dest += video_size_row)
	    memsetw((void *)dest, conp->vc_video_erase_char, width*2);
}


static void compatcon_putc(struct vc_data *conp, int c, int ypos, int xpos)
{
    u16 *p;

    if (console_blanked)
	return;

    p = (u16 *)(video_mem_base+ypos*video_size_row+xpos*2);
    scr_writew(c, p);
}


static void compatcon_putcs(struct vc_data *conp, const unsigned short *s, int count,
			    int ypos, int xpos)
{
    u16 *p;
    u16 sattr;

    if (console_blanked)
	return;

    p = (u16 *)(video_mem_base+ypos*video_size_row+xpos*2);
    while (count--)
	scr_writew(*s++, p++);
}


static void compatcon_cursor(struct vc_data *conp, int mode)
{
    switch (mode) {
	case CM_ERASE:
	    hide_cursor();
	    break;

	case CM_MOVE:
	case CM_DRAW:
	    set_cursor(conp->vc_num);
	    break;
    }
}


static int compatcon_scroll(struct vc_data *conp, int t, int b, int dir,
			    int count)
{
    if (console_blanked)
	return 0;

    compatcon_cursor(conp, CM_ERASE);

    switch (dir) {
	case SM_UP:
	    if (count > conp->vc_rows)	/* Maximum realistic size */
		count = conp->vc_rows;
	    compatcon_bmove(conp, t+count, 0, t, 0, b-t-count, conp->vc_cols);
	    compatcon_clear(conp, b-count, 0, count, conp->vc_cols);
	    break;

	case SM_DOWN:
	    if (count > conp->vc_rows)	/* Maximum realistic size */
		count = conp->vc_rows;
	    /*
	     *  Fixed bmove() should end Arno's frustration with copying?
	     *  Confucius says:
	     *	Man who copies in wrong direction, end up with trashed
	     *	data
	     */
	    compatcon_bmove(conp, t, 0, t+count, 0, b-t-count, conp->vc_cols);
	    compatcon_clear(conp, t, 0, count, conp->vc_cols);
	    break;

	case SM_LEFT:
	    compatcon_bmove(conp, 0, t+count, 0, t, conp->vc_rows, b-t-count);
	    compatcon_clear(conp, 0, b-count, conp->vc_rows, count);
	    break;

	case SM_RIGHT:
	    compatcon_bmove(conp, 0, t, 0, t+count, conp->vc_rows, b-t-count);
	    compatcon_clear(conp, 0, t, conp->vc_rows, count);
	    break;
    }
    return 0;
}


static inline void memmovew(u16 *to, u16 *from, unsigned int count)
{
    if ((unsigned long)to < (unsigned long)from)
	memcpyw(to, from, count);
    else {
	count /= 2;
	to += count;
	from += count;
	while (count) {
	    count--;
	    scr_writew(scr_readw(--from), --to);
	}
    }
}

static void compatcon_bmove(struct vc_data *conp, int sy, int sx, int dy,
			    int dx, int height, int width)
{
    unsigned long src, dst;
    int rows;

    if (console_blanked)
	return;

    if (sx == 0 && dx == 0 && width == video_num_columns) {
	src = video_mem_base+sy*video_size_row;
	dst = video_mem_base+dy*video_size_row;
	memmovew((u16 *)dst, (u16 *)src, height*video_size_row);
    } else if (dy < sy || (dy == sy && dx < sx)) {
	src = video_mem_base+sy*video_size_row+sx*2;
	dst = video_mem_base+dy*video_size_row+dx*2;
	for (rows = height; rows-- ;) {
	    memmovew((u16 *)dst, (u16 *)src, width*2);
	    src += video_size_row;
	    dst += video_size_row;
	}
    } else {
	src = video_mem_base+(sy+height-1)*video_size_row+sx*2;
	dst = video_mem_base+(dy+height-1)*video_size_row+dx*2;
	for (rows = height; rows-- ;) {
	    memmovew((u16 *)dst, (u16 *)src, width*2);
	    src -= video_size_row;
	    dst -= video_size_row;
	}
    }
}


static int compatcon_switch(struct vc_data *conp)
{
    return 1;
}


static int compatcon_blank(int blank)
{
    if (blank) {
	set_vesa_blanking(blank-1);
	if (blank-1 == 0)
	    vesa_blank();
	else
	    vesa_powerdown();
	return 0;
    } else {
	/* Tell console.c that it has to restore the screen itself */
	return 1;
    }
    return 0;
}


static int compatcon_get_font(struct vc_data *conp, int *w, int *h, char *data)
{
    /* this is not supported: data already points to a kernel space copy */
    return -ENOSYS;
}


static int compatcon_set_font(struct vc_data *conp, int w, int h, char *data)
{
    /* this is not supported: data already points to a kernel space copy */
    return -ENOSYS;
}

static int compatcon_set_palette(struct vc_data *conp, unsigned char *table)
{
    if (console_blanked || vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
	return -EINVAL;
    set_palette();
    return 0;
}

static int compatcon_scrolldelta(struct vc_data *conp, int lines)
{
    /* TODO */
    return -ENOSYS;
}

    /*
     *  The console `switch' structure for the console wrapper
     */

struct consw compat_con = {
    compatcon_startup, compatcon_init, compatcon_deinit, compatcon_clear,
    compatcon_putc, compatcon_putcs, compatcon_cursor, compatcon_scroll,
    compatcon_bmove, compatcon_switch, compatcon_blank, compatcon_get_font,
    compatcon_set_font, compatcon_set_palette, compatcon_scrolldelta,
    NULL, NULL
};
