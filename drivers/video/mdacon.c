/*
 *  linux/drivers/video/mdacon.c -- Low level MDA based console driver
 *
 *	(c) 1998 Andrew Apted <ajapted@netspace.net.au>
 *
 *  This file is based on the VGA console driver (vgacon.c):
 *	
 *	Created 28 Sep 1997 by Geert Uytterhoeven
 *
 *	Rewritten by Martin Mares <mj@ucw.cz>, July 1998
 *
 *  and on the old console.c, vga.c and vesa_blank.c drivers:
 *
 *	Copyright (C) 1991, 1992  Linus Torvalds
 *			    1995  Jay Estabrook
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/vt_kern.h>
#include <linux/vt_buffer.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/vga.h>


/* description of the hardware layout */

static unsigned long	mda_vram_base;		/* Base of video memory */
static unsigned long	mda_vram_len;		/* Size of video memory */
static unsigned int	mda_num_columns;	/* Number of text columns */
static unsigned int	mda_num_lines;		/* Number of text lines */

static unsigned int	mda_index_port;		/* Register select port */
static unsigned int	mda_value_port;		/* Register value port */
static unsigned int	mda_mode_port;		/* Mode control port */

/* current hardware state */

static int	mda_origin_loc=-1;
static int	mda_cursor_loc=-1;
static int	mda_cursor_size_from=-1;
static int	mda_cursor_size_to=-1;

/* console information */

static int	mda_first_vc = 12;
static int	mda_last_vc  = 15;

static struct vc_data	*mda_display_fg = NULL;

#ifdef MODULE_PARM
MODULE_PARM(mda_first_vc, "0-255i");
MODULE_PARM(mda_last_vc,  "0-255i");
#endif


/*
 * MDA could easily be classified as "pre-dinosaur hardware".
 */

static void write_mda_b(unsigned char reg, unsigned int val)
{
	unsigned long flags;

	save_flags(flags); cli();

	outb_p(reg, mda_index_port); 
	outb_p(val, mda_value_port);

	restore_flags(flags);
}

static void write_mda_w(unsigned char reg, unsigned int val)
{
	unsigned long flags;

	save_flags(flags); cli();

	outb_p(reg,   mda_index_port); outb_p(val>>8,   mda_value_port);
	outb_p(reg+1, mda_index_port); outb_p(val&0xff, mda_value_port);

	restore_flags(flags);
}

static inline void mda_set_origin(unsigned int location)
{
	if (mda_origin_loc == location)
		return;

	write_mda_w(0x0c, location >> 1);

	mda_origin_loc = location;
}

static inline void mda_set_cursor(unsigned int location) 
{
	if (mda_cursor_loc == location)
		return;

	write_mda_w(0x0e, location >> 1);

	mda_cursor_loc = location;
}

static inline void mda_set_cursor_size(int from, int to)
{
	if (mda_cursor_size_from==from && mda_cursor_size_to==to)
		return;
	
	if (from > to) {
		write_mda_b(0x0a, 0x20);	/* disable cursor */
	} else {
		write_mda_b(0x0a, from);	/* cursor start */
		write_mda_b(0x0b, to);		/* cursor end */
	}

	mda_cursor_size_from = from;
	mda_cursor_size_to   = to;
}


#ifndef MODULE
__initfunc(void mdacon_setup(char *str, int *ints))
{
	/* command line format: mdacon=<first>,<last> */

	if (ints[0] < 2)
		return;

	if (ints[1] < 0 || ints[1] >= MAX_NR_CONSOLES || 
	    ints[2] < 0 || ints[2] >= MAX_NR_CONSOLES)
		return;

	mda_first_vc = ints[1];
	mda_last_vc  = ints[2];
}
#endif

#ifdef MODULE
static const char *mdacon_startup(void)
#else
__initfunc(static const char *mdacon_startup(void))
#endif
{
	int count=0;
	u16 saved;
	u16 *p;

	mda_num_columns = 80;
	mda_num_lines   = 25;

	mda_vram_base = VGA_MAP_MEM(0xb0000);
	mda_vram_len  = 0x01000;

	mda_index_port = 0x3b4;
	mda_value_port = 0x3b5;
	mda_mode_port  = 0x3b8;

	/*  Make sure there is an MDA card present.
	 *  Are there smarter methods around?
	 */

	p = (u16 *) mda_vram_base;
	saved = scr_readw(p);

	scr_writew(0xAA55, p); if (scr_readw(p) == 0xAA55) count++;
	scr_writew(0x55AA, p); if (scr_readw(p) == 0x55AA) count++;
	scr_writew(saved, p);

	if (count != 2) {
		printk("mdacon: MDA card not detected.");
		return NULL;
	}

	printk("mdacon: MDA with %ldK of memory detected.\n",
		mda_vram_len/1024);

	return "MDA-2";
}

static void mdacon_init(struct vc_data *c, int init)
{
	c->vc_complement_mask = 0x0800;	 /* reverse video */
	c->vc_display_fg = &mda_display_fg;

	if (init) {
		c->vc_cols = mda_num_columns;
		c->vc_rows = mda_num_lines;
	} else {
		vc_resize_con(mda_num_lines, mda_num_columns, c->vc_num);
        }
	
	/* make the first MDA console visible */

	if (mda_display_fg == NULL)
		mda_display_fg = c;

	MOD_INC_USE_COUNT;
}

static void mdacon_deinit(struct vc_data *c)
{
	/* con_set_default_unimap(c->vc_num); */

	if (mda_display_fg == c)
		mda_display_fg = NULL;

	MOD_DEC_USE_COUNT;
}

static inline u16 mda_convert_attr(u16 ch)
{
	u16 attr = 0x0700;

	/* Underline and reverse-video are mutually exclusive on MDA.
	 * Since reverse-video is used for cursors and selected areas,
	 * it takes precedence. 
	 */

	if (ch & 0x0800)	attr = 0x7000;	/* reverse */
	else if (ch & 0x0400)	attr = 0x0100;	/* underline */

	return ((ch & 0x0200) << 2) | 		/* intensity */ 
		(ch & 0x8000) |			/* blink */ 
		(ch & 0x00ff) | attr;
}

static u8 mdacon_build_attr(struct vc_data *c, u8 color, u8 intensity, 
			    u8 blink, u8 underline, u8 reverse)
{
	/* The attribute is just a bit vector:
	 *
	 *	Bit 0..1 : intensity (0..2)
	 *	Bit 2    : underline
	 *	Bit 3    : reverse
	 *	Bit 7    : blink
	 */

	return (intensity & 3) |
		((underline & 1) << 2) |
		((reverse   & 1) << 3) |
		((blink     & 1) << 7);
}

static void mdacon_invert_region(struct vc_data *c, u16 *p, int count)
{
	for (; count > 0; count--) {
		scr_writew(scr_readw(p) ^ 0x0800, p++);
	}
}

#define MDA_ADDR(x,y)  ((u16 *) mda_vram_base + (y)*mda_num_columns + (x))

static void mdacon_putc(struct vc_data *c, int ch, int y, int x)
{
	scr_writew(mda_convert_attr(ch), MDA_ADDR(x, y));
}

static void mdacon_putcs(struct vc_data *c, const unsigned short *s,
		         int count, int y, int x)
{
	u16 *dest = MDA_ADDR(x, y);

	for (; count > 0; count--) {
		scr_writew(mda_convert_attr(*s++), dest++);
	}
}

static void mdacon_clear(struct vc_data *c, int y, int x, 
			  int height, int width)
{
	u16 *dest = MDA_ADDR(x, y);
	u16 eattr = mda_convert_attr(c->vc_video_erase_char);

	if (width <= 0 || height <= 0)
		return;

	if (x==0 && width==mda_num_columns) {
		scr_memsetw(dest, eattr, height*width*2);
	} else {
		for (; height > 0; height--, dest+=mda_num_columns)
			scr_memsetw(dest, eattr, width*2);
	}
}
                        
static void mdacon_bmove(struct vc_data *c, int sy, int sx, 
			 int dy, int dx, int height, int width)
{
	u16 *src, *dest;

	if (width <= 0 || height <= 0)
		return;
		
	if (sx==0 && dx==0 && width==mda_num_columns) {
		scr_memmovew(MDA_ADDR(0,dy), MDA_ADDR(0,sy), height*width*2);

	} else if (dy < sy || (dy == sy && dx < sx)) {
		src  = MDA_ADDR(sx, sy);
		dest = MDA_ADDR(dx, dy);

		for (; height > 0; height--) {
			scr_memmovew(dest, src, width*2);
			src  += mda_num_columns;
			dest += mda_num_columns;
		}
	} else {
		src  = MDA_ADDR(sx, sy+height-1);
		dest = MDA_ADDR(dx, dy+height-1);

		for (; height > 0; height--) {
			scr_memmovew(dest, src, width*2);
			src  -= mda_num_columns;
			dest -= mda_num_columns;
		}
	}
}

static int mdacon_switch(struct vc_data *c)
{
	return 1;	/* redrawing needed */
}

static int mdacon_set_palette(struct vc_data *c, unsigned char *table)
{
	return -EINVAL;
}

static int mdacon_blank(struct vc_data *c, int blank)
{
	if (blank) {
		outb_p(0x00, mda_mode_port);	/* disable video */
	} else {
		outb_p(0x28, mda_mode_port);	/* enable video & blinking */
	}
	
	return 0;
}

static int mdacon_font_op(struct vc_data *c, struct console_font_op *op)
{
	return -ENOSYS;
}

static int mdacon_scrolldelta(struct vc_data *c, int lines)
{
	return 0;
}

static void mdacon_cursor(struct vc_data *c, int mode)
{
	if (mode == CM_ERASE) {
		mda_set_cursor(mda_vram_len - 1);
		return;
	}

	mda_set_cursor(c->vc_y*mda_num_columns*2 + c->vc_x*2);

	switch (c->vc_cursor_type & 0x0f) {

		case CUR_LOWER_THIRD:	mda_set_cursor_size(10, 13); break;
		case CUR_LOWER_HALF:	mda_set_cursor_size(7,  13); break;
		case CUR_TWO_THIRDS:	mda_set_cursor_size(4,  13); break;
		case CUR_BLOCK:		mda_set_cursor_size(1,  13); break;
		case CUR_NONE:		mda_set_cursor_size(14, 13); break;
		default:		mda_set_cursor_size(12, 13); break;
    }
}

static int mdacon_scroll(struct vc_data *c, int t, int b, int dir, int lines)
{
	u16 eattr = mda_convert_attr(c->vc_video_erase_char);

	if (!lines)
		return 0;

	if (lines > c->vc_rows)   /* maximum realistic size */
		lines = c->vc_rows;

	switch (dir) {

	case SM_UP:
		scr_memmovew(MDA_ADDR(0,t), MDA_ADDR(0,t+lines),
				(b-t-lines)*mda_num_columns*2);
		scr_memsetw(MDA_ADDR(0,b-lines), eattr,
				lines*mda_num_columns*2);
		break;

	case SM_DOWN:
		scr_memmovew(MDA_ADDR(0,t+lines), MDA_ADDR(0,t),
				(b-t-lines)*mda_num_columns*2);
		scr_memsetw(MDA_ADDR(0,t), eattr, lines*mda_num_columns*2);
		break;
	}

	return 0;
}


/*
 *  The console `switch' structure for the MDA based console
 */

struct consw mda_con = {
	mdacon_startup,		/* con_startup */
	mdacon_init,		/* con_init */
	mdacon_deinit,		/* con_deinit */
	mdacon_clear,		/* con_clear */
	mdacon_putc,		/* con_putc */
	mdacon_putcs,		/* con_putcs */
	mdacon_cursor,		/* con_cursor */
	mdacon_scroll,		/* con_scroll */
	mdacon_bmove,		/* con_bmove */
	mdacon_switch,		/* con_switch */
	mdacon_blank,		/* con_blank */
	mdacon_font_op,		/* con_font_op */
	mdacon_set_palette,	/* con_set_palette */
	mdacon_scrolldelta,	/* con_scrolldelta */
	NULL,			/* con_set_origin */
	NULL,			/* con_save_screen */
	mdacon_build_attr,	/* con_build_attr */
	mdacon_invert_region,	/* con_invert_region */
};

#ifdef MODULE
void mda_console_init(void)
#else
__initfunc(void mda_console_init(void))
#endif
{
	if (mda_first_vc > mda_last_vc)
		return;

	take_over_console(&mda_con, mda_first_vc, mda_last_vc, 0);
}

#ifdef MODULE

int init_module(void)
{
	mda_console_init();

	return 0;
}

void cleanup_module(void)
{
	give_up_console(&mda_con);
}

#endif
