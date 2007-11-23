/*
 *  linux/drivers/char/console.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 *
 * Virtual Consoles, Screen Blanking, Screen Dumping, Color, Graphics
 *   Chars, and VT100 enhancements by Peter MacDonald.
 *
 * Copy and paste function by Andrew Haylett,
 *   some enhancements by Alessandro Rubini.
 *
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 *
 * Rudimentary ISO 10646/Unicode/UTF-8 character set support by
 * Markus Kuhn, <mskuhn@immd4.informatik.uni-erlangen.de>.
 *
 * Dynamic allocation of consoles, aeb@cwi.nl, May 1994
 * Resizing of consoles, aeb, 940926
 *
 * Code for xterm like mouse click reporting by Peter Orbaek 20-Jul-94
 * <poe@daimi.aau.dk>
 *
 * User-defined bell sound, new setterm control sequences and printk
 * redirection by Martin Mares <mj@k332.feld.cvut.cz> 19-Nov-95
 *
 * APM screenblank bug fixed Takashi Manabe <manabe@roy.dsl.tutics.tut.jp>
 *
 * Merge with the abstract console driver by Geert Uytterhoeven
 * <Geert.Uytterhoeven@cs.kuleuven.ac.be>, Jan 1997.
 *
 *   Original m68k console driver modifications by
 *
 *     - Arno Griffioen <arno@usn.nl>
 *     - David Carter <carter@cs.bris.ac.uk>
 * 
 *   Note that the abstract console driver allows all consoles to be of
 *   potentially different sizes, so the following variables depend on the
 *   current console (currcons):
 *
 *     - video_num_columns
 *     - video_num_lines
 *     - video_size_row
 *     - can_do_color
 *
 *   The abstract console driver provides a generic interface for a text
 *   console. It supports VGA text mode, frame buffer based graphical consoles
 *   and special graphics processors that are only accessible through some
 *   registers (e.g. a TMS340x0 GSP).
 *
 *   The interface to the hardware is specified using a special structure
 *   (struct consw) which contains function pointers to console operations
 *   (see <linux/console.h> for more information).
 *
 * Support for changeable cursor shape
 * by Pavel Machek <pavel@atrey.karlin.mff.cuni.cz>, August 1997
 *
 * Ported to i386 and con_scrolldelta fixed
 * by Emmanuel Marty <core@ggi-project.org>, April 1998
 *
 * Resurrected character buffers in videoram plus lots of other trickery
 * by Martin Mares <mj@atrey.karlin.mff.cuni.cz>, July 1998
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/console_struct.h>
#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/consolemap.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/tqueue.h>
#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif

#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#include <asm/linux_logo.h>

#include "console_macros.h"


struct consw *conswitchp = NULL;

/* A bitmap for codes <32. A bit of 1 indicates that the code
 * corresponding to that bit number invokes some special action
 * (such as cursor movement) and should not be displayed as a
 * glyph unless the disp_ctrl mode is explicitly enabled.
 */
#define CTRL_ACTION 0x0d00ff81
#define CTRL_ALWAYS 0x0800f501	/* Cannot be overridden by disp_ctrl */

/*
 * Here is the default bell parameters: 750HZ, 1/8th of a second
 */
#define DEFAULT_BELL_PITCH	750
#define DEFAULT_BELL_DURATION	(HZ/8)

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

static struct tty_struct *console_table[MAX_NR_CONSOLES];
static struct termios *console_termios[MAX_NR_CONSOLES];
static struct termios *console_termios_locked[MAX_NR_CONSOLES];
struct vc vc_cons [MAX_NR_CONSOLES];

#ifndef VT_SINGLE_DRIVER
static struct consw *con_driver_map[MAX_NR_CONSOLES];
#endif

static int con_open(struct tty_struct *, struct file *);
static void vc_init(unsigned int console, unsigned int rows,
		    unsigned int cols, int do_clear);
static void blank_screen(void);
static void gotoxy(int currcons, int new_x, int new_y);
static void save_cur(int currcons);
static void reset_terminal(int currcons, int do_clear);
static void con_flush_chars(struct tty_struct *tty);
static void set_vesa_blanking(unsigned long arg);
static void set_cursor(int currcons);
static void hide_cursor(int currcons);

static int printable = 0;		/* Is console ready for printing? */

int do_poke_blanked_console = 0;
int console_blanked = 0;

static int vesa_blank_mode = 0; /* 0:none 1:suspendV 2:suspendH 3:powerdown */
static int blankinterval = 10*60*HZ;
static int vesa_off_interval = 0;

/*
 * fg_console is the current virtual console,
 * last_console is the last used one,
 * want_console is the console we want to switch to,
 * kmsg_redirect is the console for kernel messages,
 */
int fg_console = 0;
int last_console = 0;
int want_console = -1;
int kmsg_redirect = 0;

/*
 * For each existing display, we have a pointer to console currently visible
 * on that display, allowing consoles other than fg_console to be refreshed
 * appropriately. Unless the low-level driver supplies its own display_fg
 * variable, we use this one for the "master display".
 */
static struct vc_data *master_display_fg = NULL;

/*
 * Unfortunately, we need to delay tty echo when we're currently writing to the
 * console since the code is (and always was) not re-entrant, so we insert
 * all filp requests to con_task_queue instead of tq_timer and run it from
 * the console_bh.
 */
DECLARE_TASK_QUEUE(con_task_queue);

/*
 * For the same reason, we defer scrollback to the console_bh.
 */
static int scrollback_delta = 0;

/*
 *	Low-Level Functions
 */

#define IS_FG (currcons == fg_console)
#define IS_VISIBLE CON_IS_VISIBLE(vc_cons[currcons].d)

#ifdef VT_BUF_VRAM_ONLY
#define DO_UPDATE 0
#else
#define DO_UPDATE IS_VISIBLE
#endif

static inline unsigned short *screenpos(int currcons, int offset, int viewed)
{
	unsigned short *p = (unsigned short *)(visible_origin + offset);
	return p;
}

static inline void scrolldelta(int lines)
{
	scrollback_delta += lines;
	mark_bh(CONSOLE_BH);
}

static void scrup(int currcons, unsigned int t, unsigned int b, int nr)
{
	unsigned short *d, *s;

	if (t+nr >= b)
		nr = b - t - 1;
	if (b > video_num_lines || t >= b || nr < 1)
		return;
	if (IS_VISIBLE && sw->con_scroll(vc_cons[currcons].d, t, b, SM_UP, nr))
		return;
	d = (unsigned short *) (origin+video_size_row*t);
	s = (unsigned short *) (origin+video_size_row*(t+nr));
	scr_memcpyw(d, s, (b-t-nr) * video_size_row);
	scr_memsetw(d + (b-t-nr) * video_num_columns, video_erase_char, video_size_row*nr);
}

static void
scrdown(int currcons, unsigned int t, unsigned int b, int nr)
{
	unsigned short *s;
	unsigned int step;

	if (t+nr >= b)
		nr = b - t - 1;
	if (b > video_num_lines || t >= b || nr < 1)
		return;
	if (IS_VISIBLE && sw->con_scroll(vc_cons[currcons].d, t, b, SM_DOWN, nr))
		return;
	s = (unsigned short *) (origin+video_size_row*t);
	step = video_num_columns * nr;
	scr_memmovew(s + step, s, (b-t-nr)*video_size_row);
	scr_memsetw(s, video_erase_char, 2*step);
}

static void do_update_region(int currcons, unsigned long start, int count)
{
#ifndef VT_BUF_VRAM_ONLY
	unsigned int xx, yy, offset;
	u16 *p;

	if (start < origin) {
		count -= origin - start;
		start = origin;
	}
	if (count <= 0)
		return;
	offset = (start - origin) / 2;
	xx = offset % video_num_columns;
	yy = offset / video_num_columns;
	p = (u16 *) start;
	for(;;) {
		u16 attrib = scr_readw(p) & 0xff00;
		int startx = xx;
		u16 *q = p;
		while (xx < video_num_columns && count) {
			if (attrib != (scr_readw(p) & 0xff00)) {
				if (p > q)
					sw->con_putcs(vc_cons[currcons].d, q, p-q, yy, startx);
				startx = xx;
				q = p;
				attrib = scr_readw(p) & 0xff00;
			}
			p++;
			xx++;
			count--;
		}
		if (p > q)
			sw->con_putcs(vc_cons[currcons].d, q, p-q, yy, startx);
		if (!count)
			break;
		xx = 0;
		yy++;
	}
#endif
}

void update_region(int currcons, unsigned long start, int count)
{
	if (DO_UPDATE) {
		hide_cursor(currcons);
		do_update_region(currcons, start, count);
		set_cursor(currcons);
	}
}

/* Structure of attributes is hardware-dependent */

static u8 build_attr(int currcons, u8 _color, u8 _intensity, u8 _blink, u8 _underline, u8 _reverse)
{
	if (sw->con_build_attr)
		return sw->con_build_attr(vc_cons[currcons].d, _color, _intensity, _blink, _underline, _reverse);

#ifndef VT_BUF_VRAM_ONLY
/*
 * ++roman: I completely changed the attribute format for monochrome
 * mode (!can_do_color). The formerly used MDA (monochrome display
 * adapter) format didn't allow the combination of certain effects.
 * Now the attribute is just a bit vector:
 *  Bit 0..1: intensity (0..2)
 *  Bit 2   : underline
 *  Bit 3   : reverse
 *  Bit 7   : blink
 */
	{
	u8 a = color;
	if (!can_do_color)
		return _intensity |
		       (_underline ? 4 : 0) |
		       (_reverse ? 8 : 0) |
		       (_blink ? 0x80 : 0);
	if (_underline)
		a = (a & 0xf0) | ulcolor;
	else if (_intensity == 0)
		a = (a & 0xf0) | halfcolor;
	if (_reverse)
		a = ((a) & 0x88) | ((((a) >> 4) | ((a) << 4)) & 0x77);
	if (_blink)
		a ^= 0x80;
	if (_intensity == 2)
		a ^= 0x08;
	if (hi_font_mask == 0x100)
		a <<= 1;
	return a;
	}
#else
	return 0;
#endif
}

static void update_attr(int currcons)
{
	attr = build_attr(currcons, color, intensity, blink, underline, reverse ^ decscnm);
	video_erase_char = (build_attr(currcons, color, 1, 0, 0, decscnm) << 8) | ' ';
}

/* Note: inverting the screen twice should revert to the original state */

void invert_screen(int currcons, int offset, int count, int viewed)
{
	unsigned short *p;

	count /= 2;
	p = screenpos(currcons, offset, viewed);
	if (sw->con_invert_region)
		sw->con_invert_region(vc_cons[currcons].d, p, count);
#ifndef VT_BUF_VRAM_ONLY
	else {
		u16 *q = p;
		int cnt = count;

		if (!can_do_color) {
			while (cnt--) *q++ ^= 0x0800;
		} else if (hi_font_mask == 0x100) {
			while (cnt--) {
				u16 a = *q;
				a = ((a) & 0x11ff) | (((a) & 0xe000) >> 4) | (((a) & 0x0e00) << 4);
				*q++ = a;
			}
		} else {
			while (cnt--) {
				u16 a = *q;
				a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4);
				*q++ = a;
			}
		}
	}
#endif
	if (DO_UPDATE)
		do_update_region(currcons, (unsigned long) p, count);
}

/* used by selection: complement pointer position */
void complement_pos(int currcons, int offset)
{
	static unsigned short *p = NULL;
	static unsigned short old = 0;
	static unsigned short oldx = 0, oldy = 0;

	if (p) {
		scr_writew(old, p);
		if (DO_UPDATE)
			sw->con_putc(vc_cons[currcons].d, old, oldy, oldx);
	}
	if (offset == -1)
		p = NULL;
	else {
		unsigned short new;
		p = screenpos(currcons, offset, 1);
		old = scr_readw(p);
		new = old ^ complement_mask;
		scr_writew(new, p);
		if (DO_UPDATE) {
			oldx = (offset >> 1) % video_num_columns;
			oldy = (offset >> 1) / video_num_columns;
			sw->con_putc(vc_cons[currcons].d, new, oldy, oldx);
		}
	}
}

static void insert_char(int currcons, unsigned int nr)
{
	unsigned short *p, *q = (unsigned short *) pos;

	p = q + video_num_columns - nr - x;
	while (--p >= q)
		scr_writew(scr_readw(p), p + nr);
	scr_memsetw(q, video_erase_char, nr*2);
	need_wrap = 0;
	if (DO_UPDATE) {
		unsigned short oldattr = attr;
		sw->con_bmove(vc_cons[currcons].d,y,x,y,x+nr,1,
			      video_num_columns-x-nr);
		attr = video_erase_char >> 8;
		while (nr--)
			sw->con_putc(vc_cons[currcons].d,
				     video_erase_char,y,x+nr);
		attr = oldattr;
	}
}

static void delete_char(int currcons, unsigned int nr)
{
	unsigned int i = x;
	unsigned short *p = (unsigned short *) pos;

	while (++i <= video_num_columns - nr) {
		scr_writew(scr_readw(p+nr), p);
		p++;
	}
	scr_memsetw(p, video_erase_char, nr*2);
	need_wrap = 0;
	if (DO_UPDATE) {
		unsigned short oldattr = attr;
		sw->con_bmove(vc_cons[currcons].d, y, x+nr, y, x, 1,
			      video_num_columns-x-nr);
		attr = video_erase_char >> 8;
		while (nr--)
			sw->con_putc(vc_cons[currcons].d,
				     video_erase_char, y,
				     video_num_columns-1-nr);
		attr = oldattr;
	}
}

static int softcursor_original;

static void add_softcursor(int currcons)
{
	int i = scr_readw((u16 *) pos);
	u32 type = cursor_type;

	if (! (type & 0x10)) return;
	if (softcursor_original != -1) return;
	softcursor_original = i;
	i |= ((type >> 8) & 0xff00 );
	i ^= ((type) & 0xff00 );
	if ((type & 0x20) && ((softcursor_original & 0x7000) == (i & 0x7000))) i ^= 0x7000;
	if ((type & 0x40) && ((i & 0x700) == ((i & 0x7000) >> 4))) i ^= 0x0700;
	scr_writew(i, (u16 *) pos);
	if (DO_UPDATE)
		sw->con_putc(vc_cons[currcons].d, i, y, x);
}

static void hide_cursor(int currcons)
{
	if (currcons == sel_cons)
		clear_selection();
	if (softcursor_original != -1) {
		scr_writew(softcursor_original,(u16 *) pos);
		if (DO_UPDATE)
			sw->con_putc(vc_cons[currcons].d, softcursor_original, y, x);
		softcursor_original = -1;
	}
	sw->con_cursor(vc_cons[currcons].d,CM_ERASE);
}

static void set_cursor(int currcons)
{
    if (!IS_FG || console_blanked || vcmode == KD_GRAPHICS)
	return;
    if (deccm) {
	if (currcons == sel_cons)
		clear_selection();
	add_softcursor(currcons);
	if ((cursor_type & 0x0f) != 1)
	    sw->con_cursor(vc_cons[currcons].d,CM_DRAW);
    } else
	hide_cursor(currcons);
}

static void set_origin(int currcons)
{
	if (!IS_VISIBLE ||
	    !sw->con_set_origin ||
	    !sw->con_set_origin(vc_cons[currcons].d))
		origin = (unsigned long) screenbuf;
	visible_origin = origin;
	scr_end = origin + screenbuf_size;
	pos = origin + video_size_row*y + 2*x;
}

static inline void save_screen(int currcons)
{
	if (sw->con_save_screen)
		sw->con_save_screen(vc_cons[currcons].d);
}

/*
 *	Redrawing of screen
 */

void redraw_screen(int new_console, int is_switch)
{
	int redraw = 1;
	int currcons, old_console;
	static int lock = 0;

	if (lock)
		return;
	if (!vc_cons_allocated(new_console)) {
		/* strange ... */
		printk("redraw_screen: tty %d not allocated ??\n", new_console+1);
		return;
	}
	lock = 1;

	if (is_switch) {
		currcons = fg_console;
		hide_cursor(currcons);
		if (fg_console != new_console) {
			struct vc_data **display = vc_cons[new_console].d->vc_display_fg;
			old_console = (*display) ? (*display)->vc_num : fg_console;
			*display = vc_cons[new_console].d;
			fg_console = new_console;
			currcons = old_console;
			if (!IS_VISIBLE) {
				save_screen(currcons);
				set_origin(currcons);
			}
			currcons = new_console;
			if (old_console == new_console)
				redraw = 0;
		}
	} else {
		currcons = new_console;
		hide_cursor(currcons);
	}

	if (redraw) {
		set_origin(currcons);
		set_palette(currcons);
		if (sw->con_switch(vc_cons[currcons].d) && vcmode != KD_GRAPHICS)
			/* Update the screen contents */
			do_update_region(currcons, origin, screenbuf_size/2);
	}
	set_cursor(currcons);
	if (is_switch) {
		set_leds();
		compute_shiftstate();
	}
	lock = 0;
}

/*
 *	Allocation, freeing and resizing of VTs.
 */

int vc_cons_allocated(unsigned int i)
{
	return (i < MAX_NR_CONSOLES && vc_cons[i].d);
}

static void visual_init(int currcons, int init)
{
    /* ++Geert: sw->con_init determines console size */
    sw = conswitchp;
#ifndef VT_SINGLE_DRIVER
    if (con_driver_map[currcons])
	sw = con_driver_map[currcons];
#endif
    cons_num = currcons;
    display_fg = &master_display_fg;
    vc_cons[currcons].d->vc_uni_pagedir_loc = &vc_cons[currcons].d->vc_uni_pagedir;
    vc_cons[currcons].d->vc_uni_pagedir = 0;
    hi_font_mask = 0;
    complement_mask = 0;
    can_do_color = 0;
    sw->con_init(vc_cons[currcons].d, init);
    if (!complement_mask)
        complement_mask = can_do_color ? 0x7700 : 0x0800;
    s_complement_mask = complement_mask;
    video_size_row = video_num_columns<<1;
    screenbuf_size = video_num_lines*video_size_row;
}

int vc_allocate(unsigned int currcons)	/* return 0 on success */
{
	if (currcons >= MAX_NR_CONSOLES)
		return -ENXIO;
	if (!vc_cons[currcons].d) {
	    long p, q;

	    /* prevent users from taking too much memory */
	    if (currcons >= MAX_NR_USER_CONSOLES && !capable(CAP_SYS_RESOURCE))
	      return -EPERM;

	    /* due to the granularity of kmalloc, we waste some memory here */
	    /* the alloc is done in two steps, to optimize the common situation
	       of a 25x80 console (structsize=216, screenbuf_size=4000) */
	    /* although the numbers above are not valid since long ago, the
	       point is still up-to-date and the comment still has its value
	       even if only as a historical artifact.  --mj, July 1998 */
	    p = (long) kmalloc(structsize, GFP_KERNEL);
	    if (!p)
		return -ENOMEM;
	    vc_cons[currcons].d = (struct vc_data *)p;
	    vt_cons[currcons] = (struct vt_struct *)(p+sizeof(struct vc_data));
	    visual_init(currcons, 1);
	    if (!*vc_cons[currcons].d->vc_uni_pagedir_loc)
		con_set_default_unimap(currcons);
	    q = (long)kmalloc(screenbuf_size, GFP_KERNEL);
	    if (!q) {
		kfree_s((char *) p, structsize);
		vc_cons[currcons].d = NULL;
		vt_cons[currcons] = NULL;
		return -ENOMEM;
	    }
	    screenbuf = (unsigned short *) q;
	    kmalloced = 1;
	    vc_init(currcons, video_num_lines, video_num_columns, 1);
	}
	return 0;
}

/*
 * Change # of rows and columns (0 means unchanged/the size of fg_console)
 * [this is to be used together with some user program
 * like resize that changes the hardware videomode]
 */
int vc_resize(unsigned int lines, unsigned int cols,
	      unsigned int first, unsigned int last)
{
	unsigned int cc, ll, ss, sr, todo = 0;
	unsigned int currcons = fg_console, i;
	unsigned short *newscreens[MAX_NR_CONSOLES];

	cc = (cols ? cols : video_num_columns);
	ll = (lines ? lines : video_num_lines);
	sr = cc << 1;
	ss = sr * ll;

 	for (currcons = first; currcons <= last; currcons++) {
		if (!vc_cons_allocated(currcons) ||
		    (cc == video_num_columns && ll == video_num_lines))
			newscreens[currcons] = NULL;
		else {
			unsigned short *p = (unsigned short *) kmalloc(ss, GFP_USER);
			if (!p) {
				for (i = 0; i< currcons; i++)
					if (newscreens[i])
						kfree_s(newscreens[i], ss);
				return -ENOMEM;
			}
			newscreens[currcons] = p;
			todo++;
		}
	}
	if (!todo)
		return 0;

	for (currcons = first; currcons <= last; currcons++) {
		unsigned int occ, oll, oss, osr;
		unsigned long ol, nl, nlend, rlth, rrem;
		if (!newscreens[currcons] || !vc_cons_allocated(currcons))
			continue;

		oll = video_num_lines;
		occ = video_num_columns;
		osr = video_size_row;
		oss = screenbuf_size;

		video_num_lines = ll;
		video_num_columns = cc;
		video_size_row = sr;
		screenbuf_size = ss;

		rlth = MIN(osr, sr);
		rrem = sr - rlth;
		ol = origin;
		nl = (long) newscreens[currcons];
		nlend = nl + ss;
		if (ll < oll)
			ol += (oll - ll) * osr;

		update_attr(currcons);

		while (ol < scr_end) {
			scr_memcpyw((unsigned short *) nl, (unsigned short *) ol, rlth);
			if (rrem)
				scr_memsetw((void *)(nl + rlth), video_erase_char, rrem);
			ol += osr;
			nl += sr;
		}
		if (nlend > nl)
			scr_memsetw((void *) nl, video_erase_char, nlend - nl);
		if (kmalloced)
			kfree_s(screenbuf, oss);
		screenbuf = newscreens[currcons];
		kmalloced = 1;
		screenbuf_size = ss;
		set_origin(currcons);

		/* do part of a reset_terminal() */
		top = 0;
		bottom = video_num_lines;
		gotoxy(currcons, x, y);
		save_cur(currcons);

		if (console_table[currcons]) {
			struct winsize ws, *cws = &console_table[currcons]->winsize;
			memset(&ws, 0, sizeof(ws));
			ws.ws_row = video_num_lines;
			ws.ws_col = video_num_columns;
			if ((ws.ws_row != cws->ws_row || ws.ws_col != cws->ws_col) &&
			    console_table[currcons]->pgrp > 0)
				kill_pg(console_table[currcons]->pgrp, SIGWINCH, 1);
			*cws = ws;
		}

		if (IS_VISIBLE)
			update_screen(currcons);
	}

	return 0;
}


void vc_disallocate(unsigned int currcons)
{
	if (vc_cons_allocated(currcons)) {
	    sw->con_deinit(vc_cons[currcons].d);
	    if (kmalloced)
		kfree_s(screenbuf, screenbuf_size);
	    if (currcons >= MIN_NR_CONSOLES)
		kfree_s(vc_cons[currcons].d, structsize);
	    vc_cons[currcons].d = NULL;
	}
}

/*
 *	VT102 emulator
 */

#define set_kbd(x) set_vc_kbd_mode(kbd_table+currcons,x)
#define clr_kbd(x) clr_vc_kbd_mode(kbd_table+currcons,x)
#define is_kbd(x) vc_kbd_mode(kbd_table+currcons,x)

#define decarm		VC_REPEAT
#define decckm		VC_CKMODE
#define kbdapplic	VC_APPLIC
#define lnm		VC_CRLF

/*
 * this is what the terminal answers to a ESC-Z or csi0c query.
 */
#define VT100ID "\033[?1;2c"
#define VT102ID "\033[?6c"

unsigned char color_table[] = { 0, 4, 2, 6, 1, 5, 3, 7,
				       8,12,10,14, 9,13,11,15 };

/* the default colour table, for VGA+ colour systems */
int default_red[] = {0x00,0xaa,0x00,0xaa,0x00,0xaa,0x00,0xaa,
    0x55,0xff,0x55,0xff,0x55,0xff,0x55,0xff};
int default_grn[] = {0x00,0x00,0xaa,0x55,0x00,0x00,0xaa,0xaa,
    0x55,0x55,0xff,0xff,0x55,0x55,0xff,0xff};
int default_blu[] = {0x00,0x00,0x00,0x00,0xaa,0xaa,0xaa,0xaa,
    0x55,0x55,0x55,0x55,0xff,0xff,0xff,0xff};

/*
 * gotoxy() must verify all boundaries, because the arguments
 * might also be negative. If the given position is out of
 * bounds, the cursor is placed at the nearest margin.
 */
static void gotoxy(int currcons, int new_x, int new_y)
{
	int min_y, max_y;

	if (new_x < 0)
		x = 0;
	else
		if (new_x >= video_num_columns)
			x = video_num_columns - 1;
		else
			x = new_x;
 	if (decom) {
		min_y = top;
		max_y = bottom;
	} else {
		min_y = 0;
		max_y = video_num_lines;
	}
	if (new_y < min_y)
		y = min_y;
	else if (new_y >= max_y)
		y = max_y - 1;
	else
		y = new_y;
	pos = origin + y*video_size_row + (x<<1);
	need_wrap = 0;
}

/* for absolute user moves, when decom is set */
static void gotoxay(int currcons, int new_x, int new_y)
{
	gotoxy(currcons, new_x, decom ? (top+new_y) : new_y);
}

void scrollback(int lines)
{
	int currcons = fg_console;

	if (!lines)
		lines = video_num_lines/2;
	scrolldelta(-lines);
}

void scrollfront(int lines)
{
	int currcons = fg_console;

	if (!lines)
		lines = video_num_lines/2;
	scrolldelta(lines);
}

static void lf(int currcons)
{
    	/* don't scroll if above bottom of scrolling region, or
	 * if below scrolling region
	 */
    	if (y+1 == bottom)
		scrup(currcons,top,bottom,1);
	else if (y < video_num_lines-1) {
	    	y++;
		pos += video_size_row;
	}
	need_wrap = 0;
}

static void ri(int currcons)
{
    	/* don't scroll if below top of scrolling region, or
	 * if above scrolling region
	 */
	if (y == top)
		scrdown(currcons,top,bottom,1);
	else if (y > 0) {
		y--;
		pos -= video_size_row;
	}
	need_wrap = 0;
}

static inline void cr(int currcons)
{
	pos -= x<<1;
	need_wrap = x = 0;
}

static inline void bs(int currcons)
{
	if (x) {
		pos -= 2;
		x--;
		need_wrap = 0;
	}
}

static inline void del(int currcons)
{
	/* ignored */
}

static void csi_J(int currcons, int vpar)
{
	unsigned int count;
	unsigned short * start;

	switch (vpar) {
		case 0:	/* erase from cursor to end of display */
			count = (scr_end-pos)>>1;
			start = (unsigned short *) pos;
			if (DO_UPDATE) {
				/* do in two stages */
				sw->con_clear(vc_cons[currcons].d, y, x, 1,
					      video_num_columns-x);
				sw->con_clear(vc_cons[currcons].d, y+1, 0,
					      video_num_lines-y-1,
					      video_num_columns);
			}
			break;
		case 1:	/* erase from start to cursor */
			count = ((pos-origin)>>1)+1;
			start = (unsigned short *) origin;
			if (DO_UPDATE) {
				/* do in two stages */
				sw->con_clear(vc_cons[currcons].d, 0, 0, y,
					      video_num_columns);
				sw->con_clear(vc_cons[currcons].d, y, 0, 1,
					      x + 1);
			}
			break;
		case 2: /* erase whole display */
			count = video_num_columns * video_num_lines;
			start = (unsigned short *) origin;
			if (DO_UPDATE)
				sw->con_clear(vc_cons[currcons].d, 0, 0,
					      video_num_lines,
					      video_num_columns);
			break;
		default:
			return;
	}
	scr_memsetw(start, video_erase_char, 2*count);
	need_wrap = 0;
}

static void csi_K(int currcons, int vpar)
{
	unsigned int count;
	unsigned short * start;

	switch (vpar) {
		case 0:	/* erase from cursor to end of line */
			count = video_num_columns-x;
			start = (unsigned short *) pos;
			if (DO_UPDATE)
				sw->con_clear(vc_cons[currcons].d, y, x, 1,
					      video_num_columns-x);
			break;
		case 1:	/* erase from start of line to cursor */
			start = (unsigned short *) (pos - (x<<1));
			count = x+1;
			if (DO_UPDATE)
				sw->con_clear(vc_cons[currcons].d, y, 0, 1,
					      x + 1);
			break;
		case 2: /* erase whole line */
			start = (unsigned short *) (pos - (x<<1));
			count = video_num_columns;
			if (DO_UPDATE)
				sw->con_clear(vc_cons[currcons].d, y, 0, 1,
					      video_num_columns);
			break;
		default:
			return;
	}
	scr_memsetw(start, video_erase_char, 2 * count);
	need_wrap = 0;
}

static void csi_X(int currcons, int vpar) /* erase the following vpar positions */
{					  /* not vt100? */
	int count;

	if (!vpar)
		vpar++;
	count = (vpar > video_num_columns-x) ? (video_num_columns-x) : vpar;

	scr_memsetw((unsigned short *) pos, video_erase_char, 2 * count);
	if (DO_UPDATE)
		sw->con_clear(vc_cons[currcons].d, y, x, 1, count);
	need_wrap = 0;
}

static void default_attr(int currcons)
{
	intensity = 1;
	underline = 0;
	reverse = 0;
	blink = 0;
	color = def_color;
}

static void csi_m(int currcons)
{
	int i;

	for (i=0;i<=npar;i++)
		switch (par[i]) {
			case 0:	/* all attributes off */
				default_attr(currcons);
				break;
			case 1:
				intensity = 2;
				break;
			case 2:
				intensity = 0;
				break;
			case 4:
				underline = 1;
				break;
			case 5:
				blink = 1;
				break;
			case 7:
				reverse = 1;
				break;
			case 10: /* ANSI X3.64-1979 (SCO-ish?)
				  * Select primary font, don't display
				  * control chars if defined, don't set
				  * bit 8 on output.
				  */
				translate = set_translate(charset == 0
						? G0_charset
						: G1_charset);
				disp_ctrl = 0;
				toggle_meta = 0;
				break;
			case 11: /* ANSI X3.64-1979 (SCO-ish?)
				  * Select first alternate font, lets
				  * chars < 32 be displayed as ROM chars.
				  */
				translate = set_translate(IBMPC_MAP);
				disp_ctrl = 1;
				toggle_meta = 0;
				break;
			case 12: /* ANSI X3.64-1979 (SCO-ish?)
				  * Select second alternate font, toggle
				  * high bit before displaying as ROM char.
				  */
				translate = set_translate(IBMPC_MAP);
				disp_ctrl = 1;
				toggle_meta = 1;
				break;
			case 21:
			case 22:
				intensity = 1;
				break;
			case 24:
				underline = 0;
				break;
			case 25:
				blink = 0;
				break;
			case 27:
				reverse = 0;
				break;
			case 38: /* ANSI X3.64-1979 (SCO-ish?)
				  * Enables underscore, white foreground
				  * with white underscore (Linux - use
				  * default foreground).
				  */
				color = (def_color & 0x0f) | background;
				underline = 1;
				break;
			case 39: /* ANSI X3.64-1979 (SCO-ish?)
				  * Disable underline option.
				  * Reset colour to default? It did this
				  * before...
				  */
				color = (def_color & 0x0f) | background;
				underline = 0;
				break;
			case 49:
				color = (def_color & 0xf0) | foreground;
				break;
			default:
				if (par[i] >= 30 && par[i] <= 37)
					color = color_table[par[i]-30]
						| background;
				else if (par[i] >= 40 && par[i] <= 47)
					color = (color_table[par[i]-40]<<4)
						| foreground;
				break;
		}
	update_attr(currcons);
}

static void respond_string(const char * p, struct tty_struct * tty)
{
	while (*p) {
		tty_insert_flip_char(tty, *p, 0);
		p++;
	}
	con_schedule_flip(tty);
}

static void cursor_report(int currcons, struct tty_struct * tty)
{
	char buf[40];

	sprintf(buf, "\033[%d;%dR", y + (decom ? top+1 : 1), x+1);
	respond_string(buf, tty);
}

static inline void status_report(struct tty_struct * tty)
{
	respond_string("\033[0n", tty);	/* Terminal ok */
}

static inline void respond_ID(struct tty_struct * tty)
{
	respond_string(VT102ID, tty);
}

void mouse_report(struct tty_struct * tty, int butt, int mrx, int mry)
{
	char buf[8];

	sprintf(buf, "\033[M%c%c%c", (char)(' ' + butt), (char)('!' + mrx),
		(char)('!' + mry));
	respond_string(buf, tty);
}

/* invoked via ioctl(TIOCLINUX) and through set_selection */
int mouse_reporting(void)
{
	int currcons = fg_console;

	return report_mouse;
}

static void set_mode(int currcons, int on_off)
{
	int i;

	for (i=0; i<=npar; i++)
		if (ques) switch(par[i]) {	/* DEC private modes set/reset */
			case 1:			/* Cursor keys send ^[Ox/^[[x */
				if (on_off)
					set_kbd(decckm);
				else
					clr_kbd(decckm);
				break;
			case 3:	/* 80/132 mode switch unimplemented */
				deccolm = on_off;
#if 0
				(void) vc_resize(video_num_lines, deccolm ? 132 : 80);
				/* this alone does not suffice; some user mode
				   utility has to change the hardware regs */
#endif
				break;
			case 5:			/* Inverted screen on/off */
				if (decscnm != on_off) {
					decscnm = on_off;
					invert_screen(currcons, 0, screenbuf_size, 0);
					update_attr(currcons);
				}
				break;
			case 6:			/* Origin relative/absolute */
				decom = on_off;
				gotoxay(currcons,0,0);
				break;
			case 7:			/* Autowrap on/off */
				decawm = on_off;
				break;
			case 8:			/* Autorepeat on/off */
				if (on_off)
					set_kbd(decarm);
				else
					clr_kbd(decarm);
				break;
			case 9:
				report_mouse = on_off ? 1 : 0;
				break;
			case 25:		/* Cursor on/off */
				deccm = on_off;
				break;
			case 1000:
				report_mouse = on_off ? 2 : 0;
				break;
		} else switch(par[i]) {		/* ANSI modes set/reset */
			case 3:			/* Monitor (display ctrls) */
				disp_ctrl = on_off;
				break;
			case 4:			/* Insert Mode on/off */
				decim = on_off;
				break;
			case 20:		/* Lf, Enter == CrLf/Lf */
				if (on_off)
					set_kbd(lnm);
				else
					clr_kbd(lnm);
				break;
		}
}

static void setterm_command(int currcons)
{
	switch(par[0]) {
		case 1:	/* set color for underline mode */
			if (can_do_color && par[1] < 16) {
				ulcolor = color_table[par[1]];
				if (underline)
					update_attr(currcons);
			}
			break;
		case 2:	/* set color for half intensity mode */
			if (can_do_color && par[1] < 16) {
				halfcolor = color_table[par[1]];
				if (intensity == 0)
					update_attr(currcons);
			}
			break;
		case 8:	/* store colors as defaults */
			def_color = attr;
			default_attr(currcons);
			update_attr(currcons);
			break;
		case 9:	/* set blanking interval */
			blankinterval = ((par[1] < 60) ? par[1] : 60) * 60 * HZ;
			poke_blanked_console();
			break;
		case 10: /* set bell frequency in Hz */
			if (npar >= 1)
				bell_pitch = par[1];
			else
				bell_pitch = DEFAULT_BELL_PITCH;
			break;
		case 11: /* set bell duration in msec */
			if (npar >= 1)
				bell_duration = (par[1] < 2000) ?
					par[1]*HZ/1000 : 0;
			else
				bell_duration = DEFAULT_BELL_DURATION;
			break;
		case 12: /* bring specified console to the front */
			if (par[1] >= 1 && vc_cons_allocated(par[1]-1))
				set_console(par[1] - 1);
			break;
		case 13: /* unblank the screen */
			poke_blanked_console();
			break;
		case 14: /* set vesa powerdown interval */
			vesa_off_interval = ((par[1] < 60) ? par[1] : 60) * 60 * HZ;
			break;
	}
}

static void insert_line(int currcons, unsigned int nr)
{
	scrdown(currcons,y,bottom,nr);
	need_wrap = 0;
}


static void delete_line(int currcons, unsigned int nr)
{
	scrup(currcons,y,bottom,nr);
	need_wrap = 0;
}

static void csi_at(int currcons, unsigned int nr)
{
	if (nr > video_num_columns - x)
		nr = video_num_columns - x;
	else if (!nr)
		nr = 1;
	insert_char(currcons, nr);
}

static void csi_L(int currcons, unsigned int nr)
{
	if (nr > video_num_lines - y)
		nr = video_num_lines - y;
	else if (!nr)
		nr = 1;
	insert_line(currcons, nr);
}

static void csi_P(int currcons, unsigned int nr)
{
	if (nr > video_num_columns - x)
		nr = video_num_columns - x;
	else if (!nr)
		nr = 1;
	delete_char(currcons, nr);
}

static void csi_M(int currcons, unsigned int nr)
{
	if (nr > video_num_lines - y)
		nr = video_num_lines - y;
	else if (!nr)
		nr=1;
	delete_line(currcons, nr);
}

static void save_cur(int currcons)
{
	saved_x		= x;
	saved_y		= y;
	s_intensity	= intensity;
	s_underline	= underline;
	s_blink		= blink;
	s_reverse	= reverse;
	s_charset	= charset;
	s_color		= color;
	saved_G0	= G0_charset;
	saved_G1	= G1_charset;
}

static void restore_cur(int currcons)
{
	gotoxy(currcons,saved_x,saved_y);
	intensity	= s_intensity;
	underline	= s_underline;
	blink		= s_blink;
	reverse		= s_reverse;
	charset		= s_charset;
	color		= s_color;
	G0_charset	= saved_G0;
	G1_charset	= saved_G1;
	translate	= set_translate(charset ? G1_charset : G0_charset);
	update_attr(currcons);
	need_wrap = 0;
}

enum { ESnormal, ESesc, ESsquare, ESgetpars, ESgotpars, ESfunckey,
	EShash, ESsetG0, ESsetG1, ESpercent, ESignore, ESnonstd,
	ESpalette };

static void reset_terminal(int currcons, int do_clear)
{
	top		= 0;
	bottom		= video_num_lines;
	vc_state	= ESnormal;
	ques		= 0;
	translate	= set_translate(LAT1_MAP);
	G0_charset	= LAT1_MAP;
	G1_charset	= GRAF_MAP;
	charset		= 0;
	need_wrap	= 0;
	report_mouse	= 0;
	utf             = 0;
	utf_count       = 0;

	disp_ctrl	= 0;
	toggle_meta	= 0;

	decscnm		= 0;
	decom		= 0;
	decawm		= 1;
	deccm		= 1;
	decim		= 0;

	set_kbd(decarm);
	clr_kbd(decckm);
	clr_kbd(kbdapplic);
	clr_kbd(lnm);
	kbd_table[currcons].lockstate = 0;
	kbd_table[currcons].slockstate = 0;
	kbd_table[currcons].ledmode = LED_SHOW_FLAGS;
	kbd_table[currcons].ledflagstate = kbd_table[currcons].default_ledflagstate;
	set_leds();

	cursor_type = CUR_DEFAULT;
	complement_mask = s_complement_mask;

	default_attr(currcons);
	update_attr(currcons);

	tab_stop[0]	= 0x01010100;
	tab_stop[1]	=
	tab_stop[2]	=
	tab_stop[3]	=
	tab_stop[4]	= 0x01010101;

	bell_pitch = DEFAULT_BELL_PITCH;
	bell_duration = DEFAULT_BELL_DURATION;

	gotoxy(currcons,0,0);
	save_cur(currcons);
	if (do_clear)
	    csi_J(currcons,2);
}

static void do_con_trol(struct tty_struct *tty, unsigned int currcons, int c)
{
	/*
	 *  Control characters can be used in the _middle_
	 *  of an escape sequence.
	 */
	switch (c) {
	case 0:
		return;
	case 7:
		if (bell_duration)
			kd_mksound(bell_pitch, bell_duration);
		return;
	case 8:
		bs(currcons);
		return;
	case 9:
		pos -= (x << 1);
		while (x < video_num_columns - 1) {
			x++;
			if (tab_stop[x >> 5] & (1 << (x & 31)))
				break;
		}
		pos += (x << 1);
		return;
	case 10: case 11: case 12:
		lf(currcons);
		if (!is_kbd(lnm))
			return;
	case 13:
		cr(currcons);
		return;
	case 14:
		charset = 1;
		translate = set_translate(G1_charset);
		disp_ctrl = 1;
		return;
	case 15:
		charset = 0;
		translate = set_translate(G0_charset);
		disp_ctrl = 0;
		return;
	case 24: case 26:
		vc_state = ESnormal;
		return;
	case 27:
		vc_state = ESesc;
		return;
	case 127:
		del(currcons);
		return;
	case 128+27:
		vc_state = ESsquare;
		return;
	}
	switch(vc_state) {
	case ESesc:
		vc_state = ESnormal;
		switch (c) {
		case '[':
			vc_state = ESsquare;
			return;
		case ']':
			vc_state = ESnonstd;
			return;
		case '%':
			vc_state = ESpercent;
			return;
		case 'E':
			cr(currcons);
			lf(currcons);
			return;
		case 'M':
			ri(currcons);
			return;
		case 'D':
			lf(currcons);
			return;
		case 'H':
			tab_stop[x >> 5] |= (1 << (x & 31));
			return;
		case 'Z':
			respond_ID(tty);
			return;
		case '7':
			save_cur(currcons);
			return;
		case '8':
			restore_cur(currcons);
			return;
		case '(':
			vc_state = ESsetG0;
			return;
		case ')':
			vc_state = ESsetG1;
			return;
		case '#':
			vc_state = EShash;
			return;
		case 'c':
			reset_terminal(currcons,1);
			return;
		case '>':  /* Numeric keypad */
			clr_kbd(kbdapplic);
			return;
		case '=':  /* Appl. keypad */
			set_kbd(kbdapplic);
			return;
		}
		return;
	case ESnonstd:
		if (c=='P') {   /* palette escape sequence */
			for (npar=0; npar<NPAR; npar++)
				par[npar] = 0 ;
			npar = 0 ;
			vc_state = ESpalette;
			return;
		} else if (c=='R') {   /* reset palette */
			reset_palette(currcons);
			vc_state = ESnormal;
		} else
			vc_state = ESnormal;
		return;
	case ESpalette:
		if ( (c>='0'&&c<='9') || (c>='A'&&c<='F') || (c>='a'&&c<='f') ) {
			par[npar++] = (c>'9' ? (c&0xDF)-'A'+10 : c-'0') ;
			if (npar==7) {
				int i = par[0]*3, j = 1;
				palette[i] = 16*par[j++];
				palette[i++] += par[j++];
				palette[i] = 16*par[j++];
				palette[i++] += par[j++];
				palette[i] = 16*par[j++];
				palette[i] += par[j];
				set_palette(currcons);
				vc_state = ESnormal;
			}
		} else
			vc_state = ESnormal;
		return;
	case ESsquare:
		for(npar = 0 ; npar < NPAR ; npar++)
			par[npar] = 0;
		npar = 0;
		vc_state = ESgetpars;
		if (c == '[') { /* Function key */
			vc_state=ESfunckey;
			return;
		}
		ques = (c=='?');
		if (ques)
			return;
	case ESgetpars:
		if (c==';' && npar<NPAR-1) {
			npar++;
			return;
		} else if (c>='0' && c<='9') {
			par[npar] *= 10;
			par[npar] += c-'0';
			return;
		} else vc_state=ESgotpars;
	case ESgotpars:
		vc_state = ESnormal;
		switch(c) {
		case 'h':
			set_mode(currcons,1);
			return;
		case 'l':
			set_mode(currcons,0);
			return;
		case 'c':
			if (ques) {
				if (par[0])
					cursor_type = par[0] | (par[1]<<8) | (par[2]<<16);
				else
					cursor_type = CUR_DEFAULT;
				return;
			}
			break;
		case 'm':
			if (ques) {
				clear_selection();
				if (par[0])
					complement_mask = par[0]<<8 | par[1];
				else
					complement_mask = s_complement_mask;
				return;
			}
			break;
		case 'n':
			if (!ques) {
				if (par[0] == 5)
					status_report(tty);
				else if (par[0] == 6)
					cursor_report(currcons,tty);
			}
			return;
		}
		if (ques) {
			ques = 0;
			return;
		}
		switch(c) {
		case 'G': case '`':
			if (par[0]) par[0]--;
			gotoxy(currcons,par[0],y);
			return;
		case 'A':
			if (!par[0]) par[0]++;
			gotoxy(currcons,x,y-par[0]);
			return;
		case 'B': case 'e':
			if (!par[0]) par[0]++;
			gotoxy(currcons,x,y+par[0]);
			return;
		case 'C': case 'a':
			if (!par[0]) par[0]++;
			gotoxy(currcons,x+par[0],y);
			return;
		case 'D':
			if (!par[0]) par[0]++;
			gotoxy(currcons,x-par[0],y);
			return;
		case 'E':
			if (!par[0]) par[0]++;
			gotoxy(currcons,0,y+par[0]);
			return;
		case 'F':
			if (!par[0]) par[0]++;
			gotoxy(currcons,0,y-par[0]);
			return;
		case 'd':
			if (par[0]) par[0]--;
			gotoxay(currcons,x,par[0]);
			return;
		case 'H': case 'f':
			if (par[0]) par[0]--;
			if (par[1]) par[1]--;
			gotoxay(currcons,par[1],par[0]);
			return;
		case 'J':
			csi_J(currcons,par[0]);
			return;
		case 'K':
			csi_K(currcons,par[0]);
			return;
		case 'L':
			csi_L(currcons,par[0]);
			return;
		case 'M':
			csi_M(currcons,par[0]);
			return;
		case 'P':
			csi_P(currcons,par[0]);
			return;
		case 'c':
			if (!par[0])
				respond_ID(tty);
			return;
		case 'g':
			if (!par[0])
				tab_stop[x >> 5] &= ~(1 << (x & 31));
			else if (par[0] == 3) {
				tab_stop[0] =
					tab_stop[1] =
					tab_stop[2] =
					tab_stop[3] =
					tab_stop[4] = 0;
			}
			return;
		case 'm':
			csi_m(currcons);
			return;
		case 'q': /* DECLL - but only 3 leds */
			/* map 0,1,2,3 to 0,1,2,4 */
			if (par[0] < 4)
				setledstate(kbd_table + currcons,
					    (par[0] < 3) ? par[0] : 4);
			return;
		case 'r':
			if (!par[0])
				par[0]++;
			if (!par[1])
				par[1] = video_num_lines;
			/* Minimum allowed region is 2 lines */
			if (par[0] < par[1] &&
			    par[1] <= video_num_lines) {
				top=par[0]-1;
				bottom=par[1];
				gotoxay(currcons,0,0);
			}
			return;
		case 's':
			save_cur(currcons);
			return;
		case 'u':
			restore_cur(currcons);
			return;
		case 'X':
			csi_X(currcons, par[0]);
			return;
		case '@':
			csi_at(currcons,par[0]);
			return;
		case ']': /* setterm functions */
			setterm_command(currcons);
			return;
		}
		return;
	case ESpercent:
		vc_state = ESnormal;
		switch (c) {
		case '@':  /* defined in ISO 2022 */
			utf = 0;
			return;
		case 'G':  /* prelim official escape code */
		case '8':  /* retained for compatibility */
			utf = 1;
			return;
		}
		return;
	case ESfunckey:
		vc_state = ESnormal;
		return;
	case EShash:
		vc_state = ESnormal;
		if (c == '8') {
			/* DEC screen alignment test. kludge :-) */
			video_erase_char =
				(video_erase_char & 0xff00) | 'E';
			csi_J(currcons, 2);
			video_erase_char =
				(video_erase_char & 0xff00) | ' ';
			do_update_region(currcons, origin, screenbuf_size/2);
		}
		return;
	case ESsetG0:
		if (c == '0')
			G0_charset = GRAF_MAP;
		else if (c == 'B')
			G0_charset = LAT1_MAP;
		else if (c == 'U')
			G0_charset = IBMPC_MAP;
		else if (c == 'K')
			G0_charset = USER_MAP;
		if (charset == 0)
			translate = set_translate(G0_charset);
		vc_state = ESnormal;
		return;
	case ESsetG1:
		if (c == '0')
			G1_charset = GRAF_MAP;
		else if (c == 'B')
			G1_charset = LAT1_MAP;
		else if (c == 'U')
			G1_charset = IBMPC_MAP;
		else if (c == 'K')
			G1_charset = USER_MAP;
		if (charset == 1)
			translate = set_translate(G1_charset);
		vc_state = ESnormal;
		return;
	default:
		vc_state = ESnormal;
	}
}

static int do_con_write(struct tty_struct * tty, int from_user,
			const unsigned char *buf, int count)
{
#ifdef VT_BUF_VRAM_ONLY
#define FLUSH do { } while(0);
#else
#define FLUSH if (draw_x >= 0) { \
	sw->con_putcs(vc_cons[currcons].d, (u16 *)draw_from, (u16 *)draw_to-(u16 *)draw_from, y, draw_x); \
	draw_x = -1; \
	}
#endif

	int c, tc, ok, n = 0, draw_x = -1;
	unsigned int currcons;
	unsigned long draw_from = 0, draw_to = 0;
	struct vt_struct *vt = (struct vt_struct *)tty->driver_data;
	u16 himask, charmask;

	currcons = vt->vc_num;
	if (!vc_cons_allocated(currcons)) {
	    /* could this happen? */
	    static int error = 0;
	    if (!error) {
		error = 1;
		printk("con_write: tty %d not allocated\n", currcons+1);
	    }
	    return 0;
	}

	if (from_user) {
		/* just to make sure that noone lurks at places he shouldn't see. */
		if (verify_area(VERIFY_READ, buf, count))
			return 0; /* ?? are error codes legal here ?? */
	}

	himask = hi_font_mask;
	charmask = himask ? 0x1ff : 0xff;

	/* undraw cursor first */
	if (IS_FG)
		hide_cursor(currcons);

	disable_bh(CONSOLE_BH);
	while (!tty->stopped && count) {
		enable_bh(CONSOLE_BH);
		if (from_user)
			__get_user(c, buf);
		else
			c = *buf;
		buf++; n++; count--;
		disable_bh(CONSOLE_BH);

		if (utf) {
		    /* Combine UTF-8 into Unicode */
		    /* Incomplete characters silently ignored */
		    if(c > 0x7f) {
			if (utf_count > 0 && (c & 0xc0) == 0x80) {
				utf_char = (utf_char << 6) | (c & 0x3f);
				utf_count--;
				if (utf_count == 0)
				    tc = c = utf_char;
				else continue;
			} else {
				if ((c & 0xe0) == 0xc0) {
				    utf_count = 1;
				    utf_char = (c & 0x1f);
				} else if ((c & 0xf0) == 0xe0) {
				    utf_count = 2;
				    utf_char = (c & 0x0f);
				} else if ((c & 0xf8) == 0xf0) {
				    utf_count = 3;
				    utf_char = (c & 0x07);
				} else if ((c & 0xfc) == 0xf8) {
				    utf_count = 4;
				    utf_char = (c & 0x03);
				} else if ((c & 0xfe) == 0xfc) {
				    utf_count = 5;
				    utf_char = (c & 0x01);
				} else
				    utf_count = 0;
				continue;
			      }
		    } else {
		      tc = c;
		      utf_count = 0;
		    }
		} else {	/* no utf */
		  tc = translate[toggle_meta ? (c|0x80) : c];
		}

                /* If the original code was a control character we
                 * only allow a glyph to be displayed if the code is
                 * not normally used (such as for cursor movement) or
                 * if the disp_ctrl mode has been explicitly enabled.
                 * Certain characters (as given by the CTRL_ALWAYS
                 * bitmap) are always displayed as control characters,
                 * as the console would be pretty useless without
                 * them; to display an arbitrary font position use the
                 * direct-to-font zone in UTF-8 mode.
                 */
                ok = tc && (c >= 32 ||
                            (!utf && !(((disp_ctrl ? CTRL_ALWAYS
                                         : CTRL_ACTION) >> c) & 1)))
                        && (c != 127 || disp_ctrl)
			&& (c != 128+27);

		if (vc_state == ESnormal && ok) {
			/* Now try to find out how to display it */
			tc = conv_uni_to_pc(vc_cons[currcons].d, tc);
			if ( tc == -4 ) {
                                /* If we got -4 (not found) then see if we have
                                   defined a replacement character (U+FFFD) */
                                tc = conv_uni_to_pc(vc_cons[currcons].d, 0xfffd);

				/* One reason for the -4 can be that we just
				   did a clear_unimap();
				   try at least to show something. */
				if (tc == -4)
				     tc = c;
                        } else if ( tc == -3 ) {
                                /* Bad hash table -- hope for the best */
                                tc = c;
                        }
			if (tc & ~charmask)
                                continue; /* Conversion failed */

			if (need_wrap || decim)
				FLUSH
			if (need_wrap) {
				cr(currcons);
				lf(currcons);
			}
			if (decim)
				insert_char(currcons, 1);
			scr_writew(himask ?
				     ((attr & ~himask) << 8) + ((tc & 0x100) ? himask : 0) + (tc & 0xff) :
				     (attr << 8) + tc,
				   (u16 *) pos);
			if (DO_UPDATE && draw_x < 0) {
				draw_x = x;
				draw_from = pos;
			}
			if (x == video_num_columns - 1) {
				need_wrap = decawm;
				draw_to = pos+2;
			} else {
				x++;
				draw_to = (pos+=2);
			}
			continue;
		}
		FLUSH
		do_con_trol(tty, currcons, c);
	}
	FLUSH
	enable_bh(CONSOLE_BH);
	return n;
#undef FLUSH
}

/*
 * This is the console switching bottom half handler.
 *
 * Doing console switching in a bottom half handler allows
 * us to do the switches asynchronously (needed when we want
 * to switch due to a keyboard interrupt), while still giving
 * us the option to easily disable it to avoid races when we
 * need to write to the console.
 */
static void console_bh(void)
{
	run_task_queue(&con_task_queue);
	if (want_console >= 0) {
		if (want_console != fg_console && vc_cons_allocated(want_console)) {
			hide_cursor(fg_console);
			change_console(want_console);
			/* we only changed when the console had already
			   been allocated - a new console is not created
			   in an interrupt routine */
		}
		want_console = -1;
	}
	if (do_poke_blanked_console) { /* do not unblank for a LED change */
		do_poke_blanked_console = 0;
		poke_blanked_console();
	}
	if (scrollback_delta) {
		int currcons = fg_console;
		clear_selection();
		if (vcmode == KD_TEXT)
			sw->con_scrolldelta(vc_cons[currcons].d, scrollback_delta);
		scrollback_delta = 0;
	}
}

#ifdef CONFIG_VT_CONSOLE

/*
 *	Console on virtual terminal
 *
 * NOTE NOTE NOTE! This code can do no global locking. In particular,
 * we can't disable interrupts or bottom half handlers globally, because
 * we can be called from contexts that hold critical spinlocks, and
 * trying do get a global lock at this point will lead to deadlocks.
 */

void vt_console_print(struct console *co, const char * b, unsigned count)
{
	int currcons = fg_console;
	unsigned char c;
	static unsigned long printing = 0;
	const ushort *start;
	ushort cnt = 0;
	ushort myx = x;

	/* console busy or not yet initialized */
	if (!printable || test_and_set_bit(0, &printing))
		return;

	if (kmsg_redirect && vc_cons_allocated(kmsg_redirect - 1))
		currcons = kmsg_redirect - 1;

	if (!vc_cons_allocated(currcons)) {
		/* impossible */
		printk("vt_console_print: tty %d not allocated ??\n", currcons+1);
		goto quit;
	}

	if (vcmode != KD_TEXT)
		goto quit;

	/* undraw cursor first */
	if (IS_FG)
		hide_cursor(currcons);

	start = (ushort *)pos;

	/* Contrived structure to try to emulate original need_wrap behaviour
	 * Problems caused when we have need_wrap set on '\n' character */
	while (count--) {
		c = *b++;
		if (c == 10 || c == 13 || c == 8 || need_wrap) {
			if (cnt > 0) {
				if (IS_VISIBLE)
					sw->con_putcs(vc_cons[currcons].d, start, cnt, y, x);
				x += cnt;
				if (need_wrap)
					x--;
				cnt = 0;
			}
			if (c == 8) {		/* backspace */
				bs(currcons);
				start = (ushort *)pos;
				myx = x;
				continue;
			}
			if (c != 13)
				lf(currcons);
			cr(currcons);
			start = (ushort *)pos;
			myx = x;
			if (c == 10 || c == 13)
				continue;
		}
		scr_writew((attr << 8) + c, (unsigned short *) pos);
		cnt++;
		if (myx == video_num_columns - 1) {
			need_wrap = 1;
			continue;
		}
		pos+=2;
		myx++;
	}
	if (cnt > 0) {
		if (IS_VISIBLE)
			sw->con_putcs(vc_cons[currcons].d, start, cnt, y, x);
		x += cnt;
		if (x == video_num_columns) {
			x--;
			need_wrap = 1;
		}
	}
	set_cursor(currcons);
	poke_blanked_console();

quit:
	clear_bit(0, &printing);
}

static kdev_t vt_console_device(struct console *c)
{
	return MKDEV(TTY_MAJOR, c->index ? c->index : fg_console + 1);
}

struct console vt_console_driver = {
	"tty",
	vt_console_print,
	NULL,
	vt_console_device,
	keyboard_wait_for_keypress,
	unblank_screen,
	NULL,
	CON_PRINTBUFFER,
	-1,
	0,
	NULL
};
#endif

/*
 *	Handling of Linux-specific VC ioctls
 */

int tioclinux(struct tty_struct *tty, unsigned long arg)
{
	char type, data;

	if (tty->driver.type != TTY_DRIVER_TYPE_CONSOLE)
		return -EINVAL;
	if (current->tty != tty && !suser())
		return -EPERM;
	if (get_user(type, (char *)arg))
		return -EFAULT;
	switch (type)
	{
		case 2:
			return set_selection(arg, tty, 1);
		case 3:
			return paste_selection(tty);
		case 4:
			unblank_screen();
			return 0;
		case 5:
			return sel_loadlut(arg);
		case 6:
			
	/*
	 * Make it possible to react to Shift+Mousebutton.
	 * Note that 'shift_state' is an undocumented
	 * kernel-internal variable; programs not closely
	 * related to the kernel should not use this.
	 */
	 		data = shift_state;
			return __put_user(data, (char *) arg);
		case 7:
			data = mouse_reporting();
			return __put_user(data, (char *) arg);
		case 10:
			set_vesa_blanking(arg);
			return 0;
		case 11:	/* set kmsg redirect */
			if (!suser())
				return -EPERM;
			if (get_user(data, (char *)arg+1))
					return -EFAULT;
			kmsg_redirect = data;
			return 0;
		case 12:	/* get fg_console */
			return fg_console;
	}
	return -EINVAL;
}

/*
 *	/dev/ttyN handling
 */

static int con_write(struct tty_struct * tty, int from_user,
		     const unsigned char *buf, int count)
{
	int	retval;

	retval = do_con_write(tty, from_user, buf, count);
	con_flush_chars(tty);

	return retval;
}

static void con_put_char(struct tty_struct *tty, unsigned char ch)
{
	do_con_write(tty, 0, &ch, 1);
}

static int con_write_room(struct tty_struct *tty)
{
	if (tty->stopped)
		return 0;
	return 4096;		/* No limit, really; we're not buffering */
}

static int con_chars_in_buffer(struct tty_struct *tty)
{
	return 0;		/* we're not buffering */
}

/*
 * con_throttle and con_unthrottle are only used for
 * paste_selection(), which has to stuff in a large number of
 * characters...
 */
static void con_throttle(struct tty_struct *tty)
{
}

static void con_unthrottle(struct tty_struct *tty)
{
	struct vt_struct *vt = (struct vt_struct *) tty->driver_data;

	wake_up_interruptible(&vt->paste_wait);
}

/*
 * Turn the Scroll-Lock LED on when the tty is stopped
 */
static void con_stop(struct tty_struct *tty)
{
	int console_num;
	if (!tty)
		return;
	console_num = MINOR(tty->device) - (tty->driver.minor_start);
	if (!vc_cons_allocated(console_num))
		return;
	set_vc_kbd_led(kbd_table + console_num, VC_SCROLLOCK);
	set_leds();
}

/*
 * Turn the Scroll-Lock LED off when the console is started
 */
static void con_start(struct tty_struct *tty)
{
	int console_num;
	if (!tty)
		return;
	console_num = MINOR(tty->device) - (tty->driver.minor_start);
	if (!vc_cons_allocated(console_num))
		return;
	clr_vc_kbd_led(kbd_table + console_num, VC_SCROLLOCK);
	set_leds();
}

static void con_flush_chars(struct tty_struct *tty)
{
	struct vt_struct *vt = (struct vt_struct *)tty->driver_data;

	set_cursor(vt->vc_num);
}

/*
 * Allocate the console screen memory.
 */
static int con_open(struct tty_struct *tty, struct file * filp)
{
	unsigned int	currcons;
	int i;

	currcons = MINOR(tty->device) - tty->driver.minor_start;

	i = vc_allocate(currcons);
	if (i)
		return i;

	vt_cons[currcons]->vc_num = currcons;
	tty->driver_data = vt_cons[currcons];

	if (!tty->winsize.ws_row && !tty->winsize.ws_col) {
		tty->winsize.ws_row = video_num_lines;
		tty->winsize.ws_col = video_num_columns;
	}
	return 0;
}

static void vc_init(unsigned int currcons, unsigned int rows, unsigned int cols, int do_clear)
{
	int j, k ;

	video_num_columns = cols;
	video_num_lines = rows;
	video_size_row = cols<<1;
	screenbuf_size = video_num_lines * video_size_row;

	set_origin(currcons);
	pos = origin;
	reset_vc(currcons);
	for (j=k=0; j<16; j++) {
		vc_cons[currcons].d->vc_palette[k++] = default_red[j] ;
		vc_cons[currcons].d->vc_palette[k++] = default_grn[j] ;
		vc_cons[currcons].d->vc_palette[k++] = default_blu[j] ;
	}
	def_color       = 0x07;   /* white */
	ulcolor		= 0x0f;   /* bold white */
	halfcolor       = 0x08;   /* grey */
	vt_cons[currcons]->paste_wait = 0;
	reset_terminal(currcons, do_clear);
}

/*
 * This routine initializes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequence.
 */

struct tty_driver console_driver;
static int console_refcount;

__initfunc(unsigned long con_init(unsigned long kmem_start))
{
	const char *display_desc = NULL;
	unsigned int currcons = 0;

	if (conswitchp)
		display_desc = conswitchp->con_startup();
	if (!display_desc) {
		fg_console = 0;
		return kmem_start;
	}

	memset(&console_driver, 0, sizeof(struct tty_driver));
	console_driver.magic = TTY_DRIVER_MAGIC;
	console_driver.name = "tty";
	console_driver.name_base = 1;
	console_driver.major = TTY_MAJOR;
	console_driver.minor_start = 1;
	console_driver.num = MAX_NR_CONSOLES;
	console_driver.type = TTY_DRIVER_TYPE_CONSOLE;
	console_driver.init_termios = tty_std_termios;
	console_driver.flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_RESET_TERMIOS;
	console_driver.refcount = &console_refcount;
	console_driver.table = console_table;
	console_driver.termios = console_termios;
	console_driver.termios_locked = console_termios_locked;

	console_driver.open = con_open;
	console_driver.write = con_write;
	console_driver.write_room = con_write_room;
	console_driver.put_char = con_put_char;
	console_driver.flush_chars = con_flush_chars;
	console_driver.chars_in_buffer = con_chars_in_buffer;
	console_driver.ioctl = vt_ioctl;
	console_driver.stop = con_stop;
	console_driver.start = con_start;
	console_driver.throttle = con_throttle;
	console_driver.unthrottle = con_unthrottle;

	if (tty_register_driver(&console_driver))
		panic("Couldn't register console driver\n");

	timer_table[BLANK_TIMER].fn = blank_screen;
	timer_table[BLANK_TIMER].expires = 0;
	if (blankinterval) {
		timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
		timer_active |= 1<<BLANK_TIMER;
	}

	/* Unfortunately, kmalloc is not running yet */
	/* Due to kmalloc roundup allocating statically is more efficient -
	   so provide MIN_NR_CONSOLES for people with very little memory */
	for (currcons = 0; currcons < MIN_NR_CONSOLES; currcons++) {
		int j, k ;

		vc_cons[currcons].d = (struct vc_data *) kmem_start;
		kmem_start += sizeof(struct vc_data);
		vt_cons[currcons] = (struct vt_struct *) kmem_start;
		kmem_start += sizeof(struct vt_struct);
		visual_init(currcons, 1);
		screenbuf = (unsigned short *) kmem_start;
		kmem_start += screenbuf_size;
		kmalloced = 0;
		vc_init(currcons, video_num_lines, video_num_columns, 
			currcons || !sw->con_save_screen);
		for (j=k=0; j<16; j++) {
			vc_cons[currcons].d->vc_palette[k++] = default_red[j] ;
			vc_cons[currcons].d->vc_palette[k++] = default_grn[j] ;
			vc_cons[currcons].d->vc_palette[k++] = default_blu[j] ;
		}
	}
	currcons = fg_console = 0;
	master_display_fg = vc_cons[currcons].d;
	set_origin(currcons);
	save_screen(currcons);
	gotoxy(currcons,x,y);
	csi_J(currcons, 0);
	update_screen(fg_console);
	printk("Console: %s %s %dx%d",
		can_do_color ? "colour" : "mono",
		display_desc, video_num_columns, video_num_lines);
	printable = 1;
	printk("\n");

#ifdef CONFIG_VT_CONSOLE
	register_console(&vt_console_driver);
#endif

	init_bh(CONSOLE_BH, console_bh);
	
	return kmem_start;
}

#ifndef VT_SINGLE_DRIVER

static void clear_buffer_attributes(int currcons)
{
	unsigned short *p = (unsigned short *) origin;
	int count = screenbuf_size/2;
	int mask = hi_font_mask | 0xff;

	for (; count > 0; count--, p++) {
		scr_writew((scr_readw(p)&mask) | (video_erase_char&~mask), p);
	}
}

/*
 *	If we support more console drivers, this function is used
 *	when a driver wants to take over some existing consoles
 *	and become default driver for newly opened ones.
 */

void take_over_console(struct consw *csw, int first, int last, int deflt)
{
	int i, j = -1;
	const char *desc;

	desc = csw->con_startup();
	if (!desc) return;
	if (deflt)
		conswitchp = csw;

	for (i = first; i <= last; i++) {
		int old_was_color;
		int currcons = i;

		con_driver_map[i] = csw;

		if (!vc_cons[i].d || !vc_cons[i].d->vc_sw)
			continue;

		j = i;
		if (IS_VISIBLE)
			save_screen(i);
		old_was_color = vc_cons[i].d->vc_can_do_color;
		vc_cons[i].d->vc_sw->con_deinit(vc_cons[i].d);
		visual_init(i, 0);
		update_attr(i);

		/* If the console changed between mono <-> color, then
		 * the attributes in the screenbuf will be wrong.  The
		 * following resets all attributes to something sane.
		 */
		if (old_was_color != vc_cons[i].d->vc_can_do_color)
			clear_buffer_attributes(i);

		if (IS_VISIBLE)
			update_screen(i);
	}
	printk("Console: switching ");
	if (!deflt)
		printk("consoles %d-%d ", first+1, last+1);
	if (j >= 0)
		printk("to %s %s %dx%d\n",
		       vc_cons[j].d->vc_can_do_color ? "colour" : "mono",
		       desc, vc_cons[j].d->vc_cols, vc_cons[j].d->vc_rows);
	else
		printk("to %s\n", desc);
}

void give_up_console(struct consw *csw)
{
	int i;

	for(i = 0; i < MAX_NR_CONSOLES; i++)
		if (con_driver_map[i] == csw)
			con_driver_map[i] = NULL;
}

#endif

/*
 *	Screen blanking
 */

static void set_vesa_blanking(unsigned long arg)
{
    char *argp = (char *)arg + 1;
    unsigned int mode;
    get_user(mode, argp);
    vesa_blank_mode = (mode < 4) ? mode : 0;
}

static void vesa_powerdown(void)
{
    struct vc_data *c = vc_cons[fg_console].d;
    /*
     *  Power down if currently suspended (1 or 2),
     *  suspend if currently blanked (0),
     *  else do nothing (i.e. already powered down (3)).
     *  Called only if powerdown features are allowed.
     */
    switch (vesa_blank_mode) {
	case VESA_NO_BLANKING:
	    c->vc_sw->con_blank(c, VESA_VSYNC_SUSPEND+1);
	    break;
	case VESA_VSYNC_SUSPEND:
	case VESA_HSYNC_SUSPEND:
	    c->vc_sw->con_blank(c, VESA_POWERDOWN+1);
	    break;
    }
}

static void vesa_powerdown_screen(void)
{
	timer_active &= ~(1<<BLANK_TIMER);
	timer_table[BLANK_TIMER].fn = unblank_screen;

	vesa_powerdown();
}

void do_blank_screen(int entering_gfx)
{
	int currcons = fg_console;
	int i;

	if (console_blanked)
		return;

	/* entering graphics mode? */
	if (entering_gfx) {
		hide_cursor(currcons);
		save_screen(currcons);
		sw->con_blank(vc_cons[currcons].d, -1);
		console_blanked = fg_console + 1;
		set_origin(currcons);
		return;
	}

	/* don't blank graphics */
	if (vcmode != KD_TEXT) {
		console_blanked = fg_console + 1;
		return;
	}

	hide_cursor(currcons);
	if (vesa_off_interval) {
		timer_table[BLANK_TIMER].fn = vesa_powerdown_screen;
		timer_table[BLANK_TIMER].expires = jiffies + vesa_off_interval;
		timer_active |= (1<<BLANK_TIMER);
	} else {
		timer_active &= ~(1<<BLANK_TIMER);
		timer_table[BLANK_TIMER].fn = unblank_screen;
	}

	save_screen(currcons);
	/* In case we need to reset origin, blanking hook returns 1 */
	i = sw->con_blank(vc_cons[currcons].d, 1);
	console_blanked = fg_console + 1;
	if (i)
		set_origin(currcons);

#ifdef CONFIG_APM
	if (apm_display_blank())
		return;
#endif
    	if (vesa_blank_mode)
		sw->con_blank(vc_cons[currcons].d, vesa_blank_mode + 1);
}

void unblank_screen(void)
{
	int currcons;

	if (!console_blanked)
		return;
	if (!vc_cons_allocated(fg_console)) {
		/* impossible */
		printk("unblank_screen: tty %d not allocated ??\n", fg_console+1);
		return;
	}
	timer_table[BLANK_TIMER].fn = blank_screen;
	if (blankinterval) {
		timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
		timer_active |= 1<<BLANK_TIMER;
	}

	currcons = fg_console;
	console_blanked = 0;
#ifdef CONFIG_APM
	apm_display_unblank();
#endif
	if (sw->con_blank(vc_cons[currcons].d, 0))
		/* Low-level driver cannot restore -> do it ourselves */
		update_screen(fg_console);
	set_cursor(fg_console);
}

static void blank_screen(void)
{
	do_blank_screen(0);
}

void poke_blanked_console(void)
{
	timer_active &= ~(1<<BLANK_TIMER);
	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
		return;
	if (console_blanked) {
		timer_table[BLANK_TIMER].fn = unblank_screen;
		timer_table[BLANK_TIMER].expires = jiffies;	/* Now */
		timer_active |= 1<<BLANK_TIMER;
	} else if (blankinterval) {
		timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
		timer_active |= 1<<BLANK_TIMER;
	}
}

/*
 *	Palettes
 */

void set_palette(int currcons)
{
	if (vcmode != KD_GRAPHICS)
		sw->con_set_palette(vc_cons[currcons].d, color_table);
}

static int set_get_cmap(unsigned char *arg, int set)
{
    int i, j, k;

    for (i = 0; i < 16; i++)
	if (set) {
	    get_user(default_red[i], arg++);
	    get_user(default_grn[i], arg++);
	    get_user(default_blu[i], arg++);
	} else {
	    put_user(default_red[i], arg++);
	    put_user(default_grn[i], arg++);
	    put_user(default_blu[i], arg++);
	}
    if (set) {
	for (i = 0; i < MAX_NR_CONSOLES; i++)
	    if (vc_cons_allocated(i)) {
		for (j = k = 0; j < 16; j++) {
		    vc_cons[i].d->vc_palette[k++] = default_red[j];
		    vc_cons[i].d->vc_palette[k++] = default_grn[j];
		    vc_cons[i].d->vc_palette[k++] = default_blu[j];
		}
		set_palette(i);
	    }
    }
    return 0;
}

/*
 * Load palette into the DAC registers. arg points to a colour
 * map, 3 bytes per colour, 16 colours, range from 0 to 255.
 */

int con_set_cmap(unsigned char *arg)
{
	return set_get_cmap (arg,1);
}

int con_get_cmap(unsigned char *arg)
{
	return set_get_cmap (arg,0);
}

void reset_palette(int currcons)
{
	int j, k;
	for (j=k=0; j<16; j++) {
		palette[k++] = default_red[j];
		palette[k++] = default_grn[j];
		palette[k++] = default_blu[j];
	}
	set_palette(currcons);
}

/*
 *  Font switching
 *
 *  Currently we only support fonts up to 32 pixels wide, at a maximum height
 *  of 32 pixels. Userspace fontdata is stored with 32 bytes (shorts/ints, 
 *  depending on width) reserved for each character which is kinda wasty, but 
 *  this is done in order to maintain compatibility with the EGA/VGA fonts. It 
 *  is upto the actual low-level console-driver convert data into its favorite
 *  format (maybe we should add a `fontoffset' field to the `display'
 *  structure so we wont have to convert the fontdata all the time.
 *  /Jes
 */

#define max_font_size 65536

int con_font_op(int currcons, struct console_font_op *op)
{
	int rc = -EINVAL;
	int size = max_font_size, set;
	u8 *temp = NULL;
	struct console_font_op old_op;

	if (vt_cons[currcons]->vc_mode != KD_TEXT)
		goto quit;
	memcpy(&old_op, op, sizeof(old_op));
	if (op->op == KD_FONT_OP_SET) {
		if (!op->data)
			return -EINVAL;
		if (op->charcount > 512)
			goto quit;
		if (!op->height) {		/* Need to guess font height [compat] */
			int h, i;
			u8 *charmap = op->data, tmp;
			
			/* If from KDFONTOP ioctl, don't allow things which can be done in userland,
			   so that we can get rid of this soon */
			if (!(op->flags & KD_FONT_FLAG_OLD))
				goto quit;
			rc = -EFAULT;
			for (h = 32; h > 0; h--)
				for (i = 0; i < op->charcount; i++) {
					if (get_user(tmp, &charmap[32*i+h-1]))
						goto quit;
					if (tmp)
						goto nonzero;
				}
			rc = -EINVAL;
			goto quit;
		nonzero:
			rc = -EINVAL;
			op->height = h;
		}
		if (op->width > 32 || op->height > 32)
			goto quit;
		size = (op->width+7)/8 * 32 * op->charcount;
		if (size > max_font_size)
			return -ENOSPC;
		set = 1;
	} else if (op->op == KD_FONT_OP_GET)
		set = 0;
	else
		return sw->con_font_op(vc_cons[currcons].d, op);
	if (op->data) {
		temp = kmalloc(size, GFP_KERNEL);
		if (!temp)
			return -ENOMEM;
		if (set && copy_from_user(temp, op->data, size)) {
			rc = -EFAULT;
			goto quit;
		}
		op->data = temp;
	}
	disable_bh(CONSOLE_BH);
	rc = sw->con_font_op(vc_cons[currcons].d, op);
	enable_bh(CONSOLE_BH);
	op->data = old_op.data;
	if (!rc && !set) {
		int c = (op->width+7)/8 * 32 * op->charcount;
		
		if (op->data && op->charcount > old_op.charcount)
			rc = -ENOSPC;
		if (!(op->flags & KD_FONT_FLAG_OLD)) {
			if (op->width > old_op.width || 
			    op->height > old_op.height)
				rc = -ENOSPC;
		} else {
			if (op->width != 8)
				rc = -EIO;
			else if ((old_op.height && op->height > old_op.height) ||
			         op->height > 32)
				rc = -ENOSPC;
		}
		if (!rc && op->data && copy_to_user(op->data, temp, c))
			rc = -EFAULT;
	}
quit:	if (temp)
		kfree_s(temp, size);
	return rc;
}

/*
 *	Interface exported to selection and vcs.
 */

/* used by selection */
u16 screen_glyph(int currcons, int offset)
{
	u16 w = scr_readw(screenpos(currcons, offset, 1));
	u16 c = w & 0xff;

	if (w & hi_font_mask)
		c |= 0x100;
	return c;
}

/* used by vcs - note the word offset */
unsigned short *screen_pos(int currcons, int w_offset, int viewed)
{
	return screenpos(currcons, 2 * w_offset, viewed);
}

void getconsxy(int currcons, char *p)
{
	p[0] = x;
	p[1] = y;
}

void putconsxy(int currcons, char *p)
{
	gotoxy(currcons, p[0], p[1]);
	set_cursor(currcons);
}

u16 vcs_scr_readw(int currcons, u16 *org)
{
	if ((unsigned long)org == pos && softcursor_original != -1)
		return softcursor_original;
	return scr_readw(org);
}

void vcs_scr_writew(int currcons, u16 val, u16 *org)
{
	scr_writew(val, org);
	if ((unsigned long)org == pos) {
		softcursor_original = -1;
		add_softcursor(currcons);
	}
}


/*
 *	Visible symbols for modules
 */

EXPORT_SYMBOL(color_table);
EXPORT_SYMBOL(default_red);
EXPORT_SYMBOL(default_grn);
EXPORT_SYMBOL(default_blu);
EXPORT_SYMBOL(video_font_height);
EXPORT_SYMBOL(video_scan_lines);
EXPORT_SYMBOL(vc_resize);

#ifndef VT_SINGLE_DRIVER
EXPORT_SYMBOL(take_over_console);
EXPORT_SYMBOL(give_up_console);
#endif
