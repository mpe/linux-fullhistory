/*
 *  linux/drivers/char/console.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
/*
 *	console.c
 *
 * This module exports the console io functions:
 *
 *     'void do_keyboard_interrupt(void)'
 *
 *     'int vc_allocate(unsigned int console)'
 *     'int vc_cons_allocated(unsigned int console)'
 *     'int vc_resize(unsigned long lines, unsigned long cols)'
 *     'int vc_resize_con(unsigned long lines, unsigned long cols,
 *			  unsigned int currcons)'
 *     'void vc_disallocate(unsigned int currcons)'
 *
 *     'unsigned long con_init(unsigned long)'
 *     'int con_open(struct tty_struct *tty, struct file * filp)'
 *     'void con_write(struct tty_struct * tty)'
 *     'void console_print(const char * b)'
 *     'void update_screen(int new_console)'
 *
 *     'void do_blank_screen(int)'
 *     'void do_unblank_screen(void)'
 *     'void poke_blanked_console(void)'
 *
 *     'unsigned short *screen_pos(int currcons, int w_offset, int viewed)'
 *     'void complement_pos(int currcons, int offset)'
 *     'void invert_screen(int currcons, int offset, int count, int shift)'
 *
 *     'void scrollback(int lines)'
 *     'void scrollfront(int lines)'
 *
 *     'int con_get_font(char *)' 
 *     'int con_set_font(char *)'
 * 
 *     'void mouse_report(struct tty_struct * tty, int butt, int mrx, int mry)'
 *     'int mouse_reporting(void)'
 *
 *     'unsigned long get_video_num_lines(unsigned int console)'
 *     'unsigned long get_video_num_columns(unsigned int console)'
 *     'unsigned long get_video_size_row(unsigned int console)'
 *
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
 * User definable mapping table and font loading by Eugene G. Crosser,
 * <crosser@pccross.msk.su>
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
 * 680x0 LINUX support by Arno Griffioen (arno@usn.nl)
 *
 * 9-Apr-94:  Arno Griffioen: fixed scrolling and delete-char bug.
 *            Scrolling code moved to amicon.c
 *
 * 18-Apr-94: David Carter [carter@cs.bris.ac.uk]. 680x0 LINUX modified 
 *            Integrated support for new low level driver `amicon_ocs.c'
 *
 */

#define BLANK 0x0020
#define CAN_LOAD_EGA_FONTS    /* undefine if the user must not do this */

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

/*
 *  NOTE!!! We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO), but this will work even for keyboard
 * interrupts. We know interrupts aren't enabled when getting a keyboard
 * interrupt, as we use trap-gates. Hopefully all is well.
 */

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/console.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include "../../../drivers/char/kbd_kern.h"
#include "../../../drivers/char/vt_kern.h"
#include "../../../drivers/char/consolemap.h"
#include "../../../drivers/char/selection.h"


#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

struct tty_driver console_driver;
static int console_refcount;
static struct tty_struct *console_table[MAX_NR_CONSOLES];
static struct termios *console_termios[MAX_NR_CONSOLES];
static struct termios *console_termios_locked[MAX_NR_CONSOLES];

static void vc_init(unsigned int console, int do_clear);

static void update_attr(int currcons);
static void gotoxy(int currcons, int new_x, int new_y);
static void save_cur(int currcons);
static void blank_screen(void);
static void unblank_screen(void);
extern void change_console(unsigned int);
static inline void set_cursor(int currcons);
static void reset_terminal(int currcons, int do_clear);
extern void reset_vc(unsigned int new_console);
extern void vt_init(void);
extern void register_console(void (*proc)(const char *));
extern void vesa_blank(void);
extern void vesa_unblank(void);
extern void compute_shiftstate(void);
void poke_blanked_console(void);
void do_blank_screen(int);

unsigned long	video_num_lines;
unsigned long	video_num_columns;
unsigned long	video_size_row;

static int printable = 0;			/* Is console ready for printing? */
unsigned long video_font_height;	/* Height of current screen font */
unsigned long video_scan_lines;		/* Number of scan lines on screen */
unsigned long default_font_height;      /* Height of default screen font */
int	      video_mode_512ch = 0;	/* 512-character mode */
static unsigned short console_charmask = 0x0ff;

static unsigned short *vc_scrbuf[MAX_NR_CONSOLES];

/* used by kbd_bh - set by keyboard_interrupt */
       int do_poke_blanked_console = 0;
       int console_blanked = 0;
static int blankinterval = 10*60*HZ;
static int vesa_off_interval = 0;
static int vesa_blank_mode = 0; /* 0:none 1:suspendV 2:suspendH 3:powerdown */

static struct vc {
	struct vc_data *d;

	/* might add  scrmem, vt_struct, kbd  at some time,
	   to have everything in one place - the disadvantage
	   would be that vc_cons etc can no longer be static */
} vc_cons [MAX_NR_CONSOLES];
struct consw *conswitchp;

#define cols            (vc_cons[currcons].d->vc_cols)
#define rows            (vc_cons[currcons].d->vc_rows)
#define size_row        (vc_cons[currcons].d->vc_size_row)
#define screenbuf_size	(vc_cons[currcons].d->vc_screenbuf_size)
#define cons_num	(vc_cons[currcons].d->vc_num)
#define origin		(vc_cons[currcons].d->vc_origin)
#define scr_end		(vc_cons[currcons].d->vc_scr_end)
#define pos		(vc_cons[currcons].d->vc_pos)
#define top		(vc_cons[currcons].d->vc_top)
#define bottom		(vc_cons[currcons].d->vc_bottom)
#define x		(vc_cons[currcons].d->vc_x)
#define y		(vc_cons[currcons].d->vc_y)
#define vc_state	(vc_cons[currcons].d->vc_state)
#define npar		(vc_cons[currcons].d->vc_npar)
#define par		(vc_cons[currcons].d->vc_par)
#define ques		(vc_cons[currcons].d->vc_ques)
#define attr		(vc_cons[currcons].d->vc_attr)
#define saved_x		(vc_cons[currcons].d->vc_saved_x)
#define saved_y		(vc_cons[currcons].d->vc_saved_y)
#define translate	(vc_cons[currcons].d->vc_translate)
#define G0_charset	(vc_cons[currcons].d->vc_G0_charset)
#define G1_charset	(vc_cons[currcons].d->vc_G1_charset)
#define saved_G0	(vc_cons[currcons].d->vc_saved_G0)
#define saved_G1	(vc_cons[currcons].d->vc_saved_G1)
#define utf		(vc_cons[currcons].d->vc_utf)
#define utf_count	(vc_cons[currcons].d->vc_utf_count)
#define utf_char	(vc_cons[currcons].d->vc_utf_char)
#define video_mem_start	(vc_cons[currcons].d->vc_video_mem_start)
#define video_mem_end	(vc_cons[currcons].d->vc_video_mem_end)
#define video_erase_char (vc_cons[currcons].d->vc_video_erase_char)	
#define disp_ctrl	(vc_cons[currcons].d->vc_disp_ctrl)
#define toggle_meta	(vc_cons[currcons].d->vc_toggle_meta)
#define decscnm		(vc_cons[currcons].d->vc_decscnm)
#define decom		(vc_cons[currcons].d->vc_decom)
#define decawm		(vc_cons[currcons].d->vc_decawm)
#define deccm		(vc_cons[currcons].d->vc_deccm)
#define decim		(vc_cons[currcons].d->vc_decim)
#define deccolm	 	(vc_cons[currcons].d->vc_deccolm)
#define need_wrap	(vc_cons[currcons].d->vc_need_wrap)
#define has_scrolled	(vc_cons[currcons].d->vc_has_scrolled)
#define kmalloced	(vc_cons[currcons].d->vc_kmalloced)
#define report_mouse	(vc_cons[currcons].d->vc_report_mouse)
#define can_do_color	(vc_cons[currcons].d->vc_can_do_color)
#define color		(vc_cons[currcons].d->vc_color)
#define s_color		(vc_cons[currcons].d->vc_s_color)
#define def_color	(vc_cons[currcons].d->vc_def_color)
#define	foreground	(color & 0x0f)
#define background	(color & 0xf0)
#define charset		(vc_cons[currcons].d->vc_charset)
#define s_charset	(vc_cons[currcons].d->vc_s_charset)
#define	intensity	(vc_cons[currcons].d->vc_intensity)
#define	underline	(vc_cons[currcons].d->vc_underline)
#define	blink		(vc_cons[currcons].d->vc_blink)
#define	reverse		(vc_cons[currcons].d->vc_reverse)
#define	s_intensity	(vc_cons[currcons].d->vc_s_intensity)
#define	s_underline	(vc_cons[currcons].d->vc_s_underline)
#define	s_blink		(vc_cons[currcons].d->vc_s_blink)
#define	s_reverse	(vc_cons[currcons].d->vc_s_reverse)
#define	ulcolor		(vc_cons[currcons].d->vc_ulcolor)
#define	halfcolor	(vc_cons[currcons].d->vc_halfcolor)
#define tab_stop	(vc_cons[currcons].d->vc_tab_stop)
#define bell_pitch	(vc_cons[currcons].d->vc_bell_pitch)
#define bell_duration	(vc_cons[currcons].d->vc_bell_duration)
#define sw		(vc_cons[currcons].d->vc_sw)

#define vcmode		(vt_cons[currcons]->vc_mode)
#if 0 /* XXX */
#define	vtmode		(vt_cons[currcons]->vt_mode)
#define	vtpid		(vt_cons[currcons]->vt_pid)
#define	vtnewvt		(vt_cons[currcons]->vt_newvt)
#endif

#define structsize	(sizeof(struct vc_data) + sizeof(struct vt_struct))

int vc_cons_allocated(unsigned int i)
{
	return (i < MAX_NR_CONSOLES && vc_cons[i].d);
}

int vc_allocate(unsigned int currcons)		/* return 0 on success */
{
	if (currcons >= MAX_NR_CONSOLES)
	  return -ENODEV;
	if (!vc_cons[currcons].d) {
	    long p, q;

	    /* prevent users from taking too much memory */
	    if (currcons >= MAX_NR_USER_CONSOLES && !suser())
	      return -EPERM;

	    /* due to the granularity of kmalloc, we waste some memory here */
	    /* the alloc is done in two steps, to optimize the common situation
	       of a 25x80 console (structsize=216, screenbuf_size=4000) */
	    p = (long) kmalloc(structsize, GFP_KERNEL);
	    if (!p)
		return -ENOMEM;
	    vc_cons[currcons].d = (struct vc_data *) p;
	    vt_cons[currcons] = (struct vt_struct *)(p+sizeof(struct vc_data));

	    /* ++Geert: sw->con_init determines console size */
	    sw = conswitchp;
	    cons_num = currcons;
	    sw->con_init (vc_cons[currcons].d);
	    size_row = cols<<1;
	    screenbuf_size = rows*size_row;

	    q = (long) kmalloc(screenbuf_size, GFP_KERNEL);
	    if (!q) {
		kfree_s((char *) p, structsize);
		vc_cons[currcons].d = NULL;
		return -ENOMEM;
	    }
	    vc_scrbuf[currcons] = (unsigned short *) q;
	    kmalloced = 1;
	    vc_init (currcons, 1);
	}
	return 0;
}

/*
 * Change # of rows and columns (0 means the size of fg_console)
 * [this is to be used together with some user program
 * like resize that changes the hardware videomode]
 */
int vc_resize(unsigned long lines, unsigned long columns)
{
	unsigned long cc, ll, ss, sr;
	unsigned long occ, oll, oss, osr;
	unsigned short *p;
	unsigned int currcons = fg_console, i;
	unsigned short *newscreens[MAX_NR_CONSOLES];
	long ol, nl, rlth, rrem;

	cc = (columns ? columns : cols);
	ll = (lines ? lines : rows);
	sr = cc << 1;
	ss = sr * ll;

	/*
	 * Some earlier version had all consoles of potentially
	 * different sizes, but that was really messy.
	 * So now we only change if there is room for all consoles
	 * of the same size.
	 */
	for (currcons = 0; currcons < MAX_NR_CONSOLES; currcons++) {
	    if (!vc_cons_allocated(currcons))
	      newscreens[currcons] = 0;
	    else {
		p = (unsigned short *) kmalloc(ss, GFP_USER);
		if (!p) {
		    for (i = 0; i< currcons; i++)
		      if (newscreens[i])
			kfree_s(newscreens[i], ss);
		    return -ENOMEM;
		}
		newscreens[currcons] = p;
	    }
	}

#if 0 /* XXX */
	get_scrmem(fg_console);
#endif

	for (currcons = 0; currcons < MAX_NR_CONSOLES; currcons++) {
	    if (!vc_cons_allocated(currcons))
	      continue;

	    oll = rows;
	    occ = cols;
	    osr = size_row;
	    oss = screenbuf_size;

	    rows = ll;
	    cols = cc;
	    size_row = sr;
	    screenbuf_size = ss;

	    rlth = MIN(osr, sr);
	    rrem = sr - rlth;
	    ol = origin;
	    nl = (long) newscreens[currcons];
	    if (ll < oll)
	      ol += (oll - ll) * osr;

	    update_attr(currcons);
	    while (ol < scr_end) {
		/* ++Geert: TODO: Because the attributes have different meanings
                   on monochrome and color, they should really be converted if
                   can_do_color changes... */
		memcpyw((unsigned short *) nl, (unsigned short *) ol, rlth);
		if (rrem)
		  memsetw((void *)(nl + rlth), video_erase_char, rrem);
		ol += osr;
		nl += sr;
	    }

	    if (kmalloced)
	      kfree_s(vc_scrbuf[currcons], oss);
	    vc_scrbuf[currcons] = newscreens[currcons];
	    kmalloced = 1;
	    screenbuf_size = ss;

	    origin = (long) video_mem_start = vc_scrbuf[currcons];
	    scr_end = video_mem_end = ((long) video_mem_start) + ss;

	    if (scr_end > nl)
	      memsetw((void *) nl, video_erase_char, scr_end - nl);

	    /* do part of a reset_terminal() */
	    top = 0;
	    bottom = rows;
	    gotoxy(currcons, x, y);
	    save_cur(currcons);
	}

#if 0 /* XXX */
	set_scrmem(fg_console, 0);
	set_origin(fg_console);
#endif /* XXX */
	update_screen(fg_console);
	set_cursor(fg_console);

	return 0;
}

/*
 * ++Geert: Change # of rows and columns for one specific console.
 * Of course it's not messy to have all consoles of potentially different sizes,
 * except on PCish hardware :-)
 *
 * This is called by the low level console driver (arch/m68k/console/fbcon.c or
 * arch/m68k/console/txtcon.c)
 */
void vc_resize_con(unsigned long lines, unsigned long columns,
		   unsigned int currcons)
{
	unsigned long cc, ll, ss, sr;
	unsigned long occ, oll, oss, osr;
	unsigned short *newscreen;
	long ol, nl, rlth, rrem;
	struct winsize ws;

	if (!columns || !lines || currcons >= MAX_NR_CONSOLES)
	    return;

	cc = columns;
	ll = lines;
	sr = cc << 1;
	ss = sr * ll;

	if (!vc_cons_allocated(currcons))
	    newscreen = 0;
	else if (!(newscreen = (unsigned short *) kmalloc(ss, GFP_USER)))
	    return;

	if (vc_cons_allocated(currcons)) {
	    oll = rows;
	    occ = cols;
	    osr = size_row;
	    oss = screenbuf_size;

	    rows = ll;
	    cols = cc;
	    size_row = sr;
	    screenbuf_size = ss;

	    rlth = MIN(osr, sr);
	    rrem = sr - rlth;
	    ol = origin;
	    nl = (long) newscreen;
	    if (ll < oll)
	      ol += (oll - ll) * osr;

	    update_attr(currcons);
	    while (ol < scr_end) {
		/* ++Geert: TODO: Because the attributes have different meanings
                   on monochrome and color, they should really be converted if
                   can_do_color changes... */
		memcpyw((unsigned short *) nl, (unsigned short *) ol, rlth);
		if (rrem)
		  memsetw((void *)(nl + rlth), video_erase_char, rrem);
		ol += osr;
		nl += sr;
	    }

	    if (kmalloced)
	      kfree_s(vc_scrbuf[currcons], oss);
	    vc_scrbuf[currcons] = newscreen;
	    kmalloced = 1;
	    screenbuf_size = ss;

	    origin = (long) video_mem_start = vc_scrbuf[currcons];
	    scr_end = video_mem_end = ((long)video_mem_start) + ss;

	    if (scr_end > nl)
	      memsetw((void *) nl, video_erase_char, scr_end - nl);

	    /* do part of a reset_terminal() */
	    top = 0;
	    bottom = rows;
	    gotoxy(currcons, x, y);
	    save_cur(currcons);

	    ws.ws_row = rows;
	    ws.ws_col = cols;
	    if (memcmp(&ws, &console_table[currcons]->winsize, sizeof (struct winsize)) &&
	        console_table[currcons]->pgrp > 0)
		kill_pg(console_table[currcons]->pgrp, SIGWINCH, 1);
	    console_table[currcons]->winsize = ws;
	}

   if (currcons == fg_console)
      update_screen(fg_console);
}

void vc_disallocate(unsigned int currcons)
{
	if (vc_cons_allocated(currcons)) {
	    if (kmalloced)
	      kfree_s(vc_scrbuf[currcons], screenbuf_size);
	    if (currcons >= MIN_NR_CONSOLES)
	      kfree_s(vc_cons[currcons].d, structsize);
	    vc_cons[currcons].d = 0;
	}
}


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

static unsigned char color_table[] = { 0, 4, 2, 6, 1, 5, 3, 7,
				       8,12,10,14, 9,13,11,15 };

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
		if (new_x >= cols)
			x = cols - 1;
		else
			x = new_x;
 	if (decom) {
		min_y = top;
		max_y = bottom;
	} else {
		min_y = 0;
		max_y = rows;
	}
	if (new_y < min_y)
		y = min_y;
	else if (new_y >= max_y)
		y = max_y - 1;
	else
		y = new_y;
	pos = video_mem_start + y * cols + x;
	need_wrap = 0;
}

/* for absolute user moves, when decom is set */
static void gotoxay(int currcons, int new_x, int new_y)
{
	gotoxy(currcons, new_x, decom ? (top+new_y) : new_y);
}

static void hide_cursor(int currcons)
{
	sw->con_cursor(vc_cons[currcons].d,CM_ERASE);
	return;
}

static void set_cursor(int currcons)
{
	if (currcons != fg_console || console_blanked || vcmode == KD_GRAPHICS)
		return;
	if (deccm)
		sw->con_cursor(vc_cons[currcons].d,CM_DRAW);
	else
		hide_cursor(currcons);
	return;
}

void no_scroll(char *str, int *ints)
{
  /*
   * no_scroll currently does nothing on the m68k.
   */
}

/*
 * Arno:
 * Why do we need these? The keyboard code doesn't seem to do anything
 * with them either...
 */
void scrollfront(int l)
{
	return;
}

void scrollback(int l)
{
	return;
}

static void scrup(int currcons, unsigned int t, unsigned int b,
		  int nr)
{
	unsigned short *p;
	int i;

	if (b > rows || t >= b)
		return;

	memmove (video_mem_start + t * cols,
		 video_mem_start + (t + nr) * cols,
		 (b - t - nr) * cols * 2);

	p = video_mem_start + (b - nr) * cols;
	for (i = nr * cols; i > 0; i--)
	  *p++ = video_erase_char;

	if (currcons != fg_console)
	  return;
/*
 * Arno:
 * Scrolling has now been moved to amicon.c where it should have
 * been all along.
 */
	sw->con_scroll(vc_cons[currcons].d, t, b, SM_UP, nr);

	return;
	
}

static void scrdown(int currcons, unsigned int t, unsigned int b,
		    int nr)
{
	unsigned short *p;
	int i;

	if (b > rows || t >= b)
		return;

	memmove (video_mem_start + (t + nr) * cols,
		 video_mem_start + t * cols,
		 (b - t - nr) * cols * 2);

	p = video_mem_start + t * cols;
	for (i = nr * cols; i > 0; i--)
	  *p++ = video_erase_char;

	if (currcons != fg_console)
	  return;
/*
 * Arno:
 * Scrolling has now been moved to amicon.c where it should have
 * been all along.
 */
	sw->con_scroll(vc_cons[currcons].d, t, b, SM_DOWN, nr);

	return;
}

static void lf(int currcons)
{
    	/* don't scroll if above bottom of scrolling region, or
	 * if below scrolling region
	 */
    	if (y+1 == bottom)
		scrup(currcons,top,bottom, 1);
	else if (y < rows-1) {
	    	y++;
		pos += cols;
	}
	need_wrap = 0;
}

static void ri(int currcons)
{
    	/* don't scroll if below top of scrolling region, or
	 * if above scrolling region
	 */
	if (y == top)
		scrdown(currcons,top,bottom, 1);
	else if (y > 0) {
		y--;
		pos -= cols;
	}
	need_wrap = 0;
}

static inline void cr(int currcons)
{
	pos -= x;
	need_wrap = x = 0;
}

static inline void bs(int currcons)
{
	if (x) {
		pos--;
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
	unsigned long count;
	unsigned short *start;

	switch (vpar) {
		case 0:	/* erase from cursor to end of display */
			count = (video_mem_start
				 + cols * rows
				 - pos);
			start = pos;
			if (currcons != fg_console)
			  break;
			/* 680x0 do in two stages */
			sw->con_clear(vc_cons[currcons].d,y,x,1,cols-x);
			sw->con_clear(vc_cons[currcons].d,y+1,0,rows-y-1, cols);
			break;
		case 1:	/* erase from start to cursor */
			count = pos - video_mem_start + 1;
			start = video_mem_start;
			if (currcons != fg_console)
			  break;
			/* 680x0 do in two stages */
			sw->con_clear(vc_cons[currcons].d,0,0,y, cols);
			sw->con_clear(vc_cons[currcons].d,y,0,1,x + 1);
			break;
		case 2: /* erase whole display */
			count = cols * rows;
			start = video_mem_start;
			if (currcons != fg_console)
			  break;
			sw->con_clear(vc_cons[currcons].d,0,0,rows, cols);
			break;
		default:
			return;
	}
	while (count-- > 0)
	  *start++ = video_erase_char;
	need_wrap = 0;
}

static void csi_K(int currcons, int vpar)
{
	unsigned long count;
	unsigned short *start;

	switch (vpar) {
		case 0:	/* erase from cursor to end of line */
			count = cols - x;
			start = pos;
			if (currcons != fg_console)
			  break;
			sw->con_clear(vc_cons[currcons].d,y,x,1,cols-x);
			break;
		case 1:	/* erase from start of line to cursor */
			start = pos - x;
			count = x + 1;
			if (currcons != fg_console)
			  break;
			sw->con_clear(vc_cons[currcons].d,y,0,1,x + 1);
			break;
		case 2: /* erase whole line */
			start = pos - x;
			count = cols;
			if (currcons != fg_console)
			  break;
			sw->con_clear(vc_cons[currcons].d,y,0,1,cols);
			break;
		default:
			return;
	}
	while (count-- > 0)
	  *start++ = video_erase_char;
	need_wrap = 0;
}

static void csi_X(int currcons, int vpar) /* erase the following vpar positions */
{					  /* not vt100? */
	unsigned long count;
	unsigned short * start;

	if (!vpar)
		vpar++;

	start=pos;
	count=(vpar > cols-x) ? (cols-x) : vpar;

	if (currcons == fg_console)
		sw->con_clear(vc_cons[currcons].d,y,x,1,count);

	while (count-- > 0)
		*start++ = video_erase_char;
	need_wrap = 0;
}

/*
 * Arno: 
 * On 680x0 attributes are currently not used. This piece of code
 * seems hardware independent, but uses the EGA/VGA way of representing
 * attributes. 
 * TODO: modify for 680x0 and add attribute processing to putc code.
 *
 * ++roman: I completely changed the attribute format for monochrome
 * mode (!can_do_color). The formerly used MDA (monochrome display
 * adapter) format didn't allow the combination of certain effects.
 * Now the attribute is just a bit vector:
 *  Bit 0..1: intensity (0..2)
 *  Bit 2   : underline
 *  Bit 3   : reverse
 *  Bit 7   : blink
 */
static void update_attr(int currcons)
{
	if (!can_do_color) {
		/* Special treatment for monochrome */
		attr = intensity |
			(underline ? 4 : 0) |
			((reverse ^ decscnm) ? 8 : 0) |
			(blink ? 0x80 : 0);
		video_erase_char = ' ' | ((reverse ^ decscnm) ? 0x800 : 0);
		return;
	}

	attr = color;
	if (underline)
		attr = (attr & 0xf0) | ulcolor;
	else if (intensity == 0)
		attr = (attr & 0xf0) | halfcolor;
	if (reverse ^ decscnm)
		attr = reverse_video_char(attr);
	if (blink)
		attr ^= 0x80;
	if (intensity == 2)
		attr ^= 0x08;
	if (decscnm)
		video_erase_char = (reverse_video_char(color) << 8) | ' ';
	else
		video_erase_char = (color << 8) | ' ';
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
				  * Select first alternate font, let's
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
	tty_schedule_flip(tty);
}

static void cursor_report(int currcons, struct tty_struct * tty)
{
	char buf[40];

	sprintf(buf, "\033[%ld;%ldR", y + (decom ? top+1 : 1), x+1);
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

/* invoked via ioctl(TIOCLINUX) */
int mouse_reporting(void)
{
	int currcons = fg_console;

	return report_mouse;
}

static inline unsigned short *screenpos(int currcons, int offset, int viewed)
{
	unsigned short *p = (unsigned short *)(origin + offset);
#if 0
	if (viewed && currcons == fg_console)
		p -= (__real_origin - __origin);
#endif
	return p;
}

/* Note: inverting the screen twice should revert to the original state */
void invert_screen(int currcons, int offset, int count, int viewed)
{
	unsigned short *p;
	unsigned short xx, yy, oldattr;

	count /= 2;
	p = screenpos(currcons, offset, viewed);
	xx = (offset >> 1) % cols;
	yy = (offset >> 1) / cols;
	oldattr = attr;
	if (can_do_color)
		while (count--) {
			unsigned short old = scr_readw(p);
			unsigned short new = reverse_video_short(old);
			scr_writew(new, p);
			p++;
			if (currcons != fg_console)
				continue;
			attr = new >> 8;
			sw->con_putc(vc_cons[currcons].d, new & 0xff, yy, xx);
			if (++xx == cols)
				xx = 0, ++yy;
		}
	else
		while (count--) {
			unsigned short old = scr_readw(p);
			unsigned short new = old ^ 0x800;
			scr_writew(new, p);
			p++;
			if (currcons != fg_console)
				continue;
			attr = new >> 8;
			sw->con_putc(vc_cons[currcons].d, new & 0xff, yy, xx);
			if (++xx == cols)
				xx = 0, ++yy;
		}
	attr = oldattr;
}

/* used by selection: complement pointer position */
void complement_pos(int currcons, int offset)
{
	static unsigned short *p = NULL;
	static unsigned short old = 0;
	static unsigned short oldx = 0, oldy = 0;
	unsigned short new, oldattr;

	oldattr = attr;
	if (p) {
		scr_writew(old, p);
		if (currcons == fg_console) {
			attr = old >> 8;
			sw->con_putc(vc_cons[currcons].d, old & 0xff, oldy, oldx);
			attr = oldattr;
		}
	}
	if (offset == -1)
		p = NULL;
	else {
		p = screenpos(currcons, offset, 1);
		old = scr_readw(p);
		oldx = (offset >> 1) % cols;
		oldy = (offset >> 1) / cols;
		if (can_do_color)
			new = old ^ 0x7700;
		else
			new = old ^ 0x800;
		scr_writew(new, p);
		if (currcons == fg_console) {
			attr = new >> 8;
			sw->con_putc(vc_cons[currcons].d, new & 0xff, oldy, oldx);
			attr = oldattr;
		}
	}
}

/* used by selection */
unsigned short screen_word(int currcons, int offset, int viewed)
{
	return scr_readw(screenpos(currcons, offset, viewed));
}

/* used by selection - convert a screen word to a glyph number */
int scrw2glyph(unsigned short scr_word)
{
	return ( video_mode_512ch )
		? ((scr_word & 0x0800) >> 3) + (scr_word & 0x00ff)
		: scr_word & 0x00ff;
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
				(void) vc_resize(rows, deccolm ? 132 : 80);
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
				set_cursor(currcons);
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
				update_screen(par[1]-1);
			break;
		case 13: /* unblank the screen */
			unblank_screen();
			break;
		case 14: /* set vesa powerdown interval */
			vesa_off_interval = ((par[1] < 60) ? par[1] : 60) * 60 * HZ;
			break;
	}
}

static void insert_char(int currcons)
{
	int i;
	unsigned short *p = pos;

	for (i = cols - x - 2; i >= 0; i--)
	  p[i + 1] = p[i];
	*pos = video_erase_char;
	need_wrap = 0;

	if (currcons != fg_console)
	  return;

	/* Arno:
	 * Move the remainder of the line (-1 character) one spot to the right
	 */
	sw->con_bmove(vc_cons[currcons].d,y,x,y,x+1,1,(cols-x-1));
	/*
	 * Print the erase char on the current position
	 */
	sw->con_putc(vc_cons[currcons].d,(video_erase_char & 0x00ff),y,x);
}

static void csi_at(int currcons, unsigned int nr)
{
	int i;
	unsigned short *p;

	if (nr > cols - x)
		nr = cols - x;
	else if (!nr)
		nr = 1;

	p = pos + cols - x - nr;
	while (--p >= pos)
	  p[nr] = *p;
	for (i = 0; i < nr; i++)
	  *++p = video_erase_char;
	need_wrap = 0;

	if (currcons != fg_console)
	  return;

	sw->con_bmove (vc_cons[currcons].d, y, x, y, x + nr,
		       1, cols - x - nr);
	while (nr--)
	  sw->con_putc (vc_cons[currcons].d, video_erase_char & 0x00ff,
			y, x + nr);
}

static void csi_L(int currcons, unsigned int nr)
{
	if (nr > rows)
		nr = rows;
	else if (!nr)
		nr = 1;
	scrdown (currcons, y, bottom, nr);
	need_wrap = 0;
}

static void csi_P(int currcons, unsigned int nr)
{
	int i;
	unsigned short *p, *end;

	if (nr > cols - x)
		nr = cols - x;
	else if (!nr)
		nr = 1;

	p = pos;
	end = pos + cols - x - nr;
	while (p < end)
	  *p = p[nr], p++;
	for (i = 0; i < nr; i++)
	  *p++ = video_erase_char;
	need_wrap = 0;

	if (currcons != fg_console)
	  return;

	sw->con_bmove (vc_cons[currcons].d, y, x + nr, y, x,
		       1, cols - x - nr);

	while (nr--)
	  sw->con_putc (vc_cons[currcons].d, video_erase_char & 0x00ff,
			y, cols - 1 - nr);
}

static void csi_M(int currcons, unsigned int nr)
{
	if (nr > rows)
		nr = rows;
	else if (!nr)
		nr=1;
	scrup (currcons, y, bottom, nr);
	need_wrap = 0;
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
	bottom		= rows;
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

static int con_write(struct tty_struct * tty, int from_user,
		     const unsigned char *buf, int count)
{
	int c, tc, ok, n = 0;
	unsigned int currcons;
	struct vt_struct *vt = (struct vt_struct *)tty->driver_data;

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

	/* undraw cursor first */
	if (currcons == fg_console)
		hide_cursor(currcons);
	
	/* clear the selection */
	if (currcons == sel_cons)
		clear_selection();

        disable_bh(CONSOLE_BH);
	while (count) {
		enable_bh(CONSOLE_BH);
		c = from_user ? get_user(buf) : *buf;
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
                        && (c != 127 || disp_ctrl);

		if (vc_state == ESnormal && ok) {
			/* Now try to find out how to display it */
			tc = conv_uni_to_pc(tc);
			if ( tc == -4 ) {
                                /* If we got -4 (not found) then see if we have
                                   defined a replacement character (U+FFFD) */
                                tc = conv_uni_to_pc(0xfffd);
                        } else if ( tc == -3 ) {
                                /* Bad hash table -- hope for the best */
                                tc = c;
                        }
			if (tc & ~console_charmask)
				continue; /* Conversion failed */

			if (need_wrap) {
				cr(currcons);
				lf(currcons);
			}
			
#if 1 /* XXX */
                        /* DPC: 1994-04-12
                         *   Speed up overstrike mode, using new putcs.
                         *
                         * P.S. I hate 8 spaces per tab! Use Emacs!
			 */
			
			/* Only use this for the foreground console,
                           where we really draw the chars */

                        if (count > 2 &&
			    !decim && !utf && currcons == fg_console) { 
				static char putcs_buf[256];
				char   *p     = putcs_buf;
				int putcs_count  = 1;
				ushort nextx  = x + 1;

				*p++ = tc;
				*pos++ = tc | (attr << 8);
				
				if (nextx == cols) {
					sw->con_putc(vc_cons[currcons].d,
						     *putcs_buf, y, x);
					pos--;
					need_wrap = decawm;
					continue;
				}
				
				/* TAB TAB TAB - Arghh!!!! */
				
				while (count)
				{
					enable_bh(CONSOLE_BH);
					c = from_user ? get_user(buf) : *buf;
					disable_bh(CONSOLE_BH);
					tc = translate[toggle_meta ? (c|0x80) : c];
					if (!tc ||
					    !(c >= 32
					      || !(((disp_ctrl ? CTRL_ALWAYS
						   : CTRL_ACTION) >> c) & 1)))
					  break;
					tc = conv_uni_to_pc(tc);
					if (tc == -4)
					  tc = conv_uni_to_pc(0xfffd);
					else if (tc == -3)
					  tc = c;

					buf++; n++; count--;
					if (tc & ~console_charmask)
					  continue; /* Conversion failed */

					*p++ = tc;
					*pos++ = tc | (attr << 8);
					++putcs_count;
					++nextx;
					if (nextx == cols || 
					    putcs_count == sizeof (putcs_buf))
						break;
				}
				
				sw->con_putcs(vc_cons[currcons].d,
					      putcs_buf, putcs_count, y, x);
				if (nextx == cols) {
					pos--;
					x         = cols-1;
					need_wrap = decawm;
				} else
					x += putcs_count;
				continue;
                        }
			
                        /* DPC: End of putcs support */
#endif
			
			if (decim)
				insert_char(currcons);
			*pos = (attr << 8) + tc;
			if (currcons == fg_console)
				sw->con_putc(vc_cons[currcons].d,tc,y,x);
			if (x == cols - 1)
				need_wrap = decawm;
			else {
				pos++;
				x++;
			}
			continue;
		}

		/*
		 *  Control characters can be used in the _middle_
		 *  of an escape sequence.
		 */
		switch (c) {
		    case 7:
			if (bell_duration)
			    kd_mksound(bell_pitch, bell_duration);
			continue;
		    case 8:
			bs(currcons);
			continue;
		    case 9:
			pos -= x;
			while (x < cols - 1) {
				x++;
				if (tab_stop[x >> 5] & (1 << (x & 31)))
					break;
			}
			pos += x;
			continue;
		    case 10: case 11: case 12:
			lf(currcons);
			if (!is_kbd(lnm))
				continue;
		    case 13:
			cr(currcons);
			continue;
  			case 14:
			charset = 1;
			translate = set_translate(G1_charset);
			disp_ctrl = 1;
			continue;
		    case 15:
			charset = 0;
			translate = set_translate(G0_charset);
			disp_ctrl = 0;
			continue;
		    case 24: case 26:
			vc_state = ESnormal;
			continue;
		    case 27:
			vc_state = ESesc;
			continue;
		    case 127:
			del(currcons);
			continue;
		    case 128+27:
			vc_state = ESsquare;
			continue;
		}
		switch(vc_state) {
		    case ESesc:
			vc_state = ESnormal;
			switch (c) {
			    case '[':
				vc_state = ESsquare;
				continue;
			    case ']':
				vc_state = ESnonstd;
				continue;
			    case '%':
				vc_state = ESpercent;
				continue;
			    case 'E':
				cr(currcons);
				lf(currcons);
				continue;
			    case 'M':
				ri(currcons);
				continue;
			    case 'D':
				lf(currcons);
				continue;
			    case 'H':
				tab_stop[x >> 5] |= (1 << (x & 31));
				continue;
			    case 'Z':
				respond_ID(tty);
				continue;
			    case '7':
				save_cur(currcons);
				continue;
			    case '8':
				restore_cur(currcons);
				continue;
			    case '(':
				vc_state = ESsetG0;
				continue;
			    case ')':
				vc_state = ESsetG1;
				continue;
			    case '#':
				vc_state = EShash;
				continue;
			    case 'c':
				reset_terminal(currcons,1);
				continue;
			    case '>':  /* Numeric keypad */
				clr_kbd(kbdapplic);
				continue;
			    case '=':  /* Appl. keypad */
				set_kbd(kbdapplic);
				continue;
			}	
			continue;
		    case ESnonstd:
			if (c=='P') {   /* palette escape sequence */
			    for (npar=0; npar<NPAR; npar++)
				par[npar] = 0 ;
			    npar = 0 ;
			    vc_state = ESpalette;
			    continue;
			} else if (c=='R') {   /* reset palette */
#if 0
			    reset_palette (currcons);
#endif
			    vc_state = ESnormal;
			} else
			    vc_state = ESnormal;
			continue;
		case ESpalette:
			if ( (c>='0'&&c<='9') || (c>='A'&&c<='F') || (c>='a'&&c<='f') ) {
			    par[npar++] = (c>'9' ? (c&0xDF)-'A'+10 : c-'0') ;
			    if (npar==7) {
#if 0
				int i = par[0]*3, j = 1;
				palette[i] = 16*par[j++];
				palette[i++] += par[j++];
				palette[i] = 16*par[j++];
				palette[i++] += par[j++];
				palette[i] = 16*par[j++];
				palette[i] += par[j];
				set_palette() ;
#endif
				vc_state = ESnormal;
			    }
			} else
			    vc_state = ESnormal;
			continue;
		    case ESsquare:
			for(npar = 0 ; npar < NPAR ; npar++)
				par[npar] = 0;
			npar = 0;
			vc_state = ESgetpars;
			if (c == '[') { /* Function key */
				vc_state=ESfunckey;
				continue;
			}
			ques = (c=='?');
			if (ques)
				continue;
		    case ESgetpars:
			if (c==';' && npar<NPAR-1) {
				npar++;
				continue;
			} else if (c>='0' && c<='9') {
				par[npar] *= 10;
				par[npar] += c-'0';
				continue;
			} else vc_state=ESgotpars;
		    case ESgotpars:
			vc_state = ESnormal;
			switch(c) {
			    case 'h':
				set_mode(currcons,1);
				continue;
			    case 'l':
				set_mode(currcons,0);
				continue;
			    case 'n':
				if (!ques)
					if (par[0] == 5)
						status_report(tty);
					else if (par[0] == 6)
						cursor_report(currcons,tty);
				continue;
			}
			if (ques) {
				ques = 0;
				continue;
			}
			switch(c) {
			    case 'G': case '`':
				if (par[0]) par[0]--;
				gotoxy(currcons,par[0],y);
				continue;
			    case 'A':
				if (!par[0]) par[0]++;
				gotoxy(currcons,x,y-par[0]);
				continue;
			    case 'B': case 'e':
				if (!par[0]) par[0]++;
				gotoxy(currcons,x,y+par[0]);
				continue;
			    case 'C': case 'a':
				if (!par[0]) par[0]++;
				gotoxy(currcons,x+par[0],y);
				continue;
			    case 'D':
				if (!par[0]) par[0]++;
				gotoxy(currcons,x-par[0],y);
				continue;
			    case 'E':
				if (!par[0]) par[0]++;
				gotoxy(currcons,0,y+par[0]);
				continue;
			    case 'F':
				if (!par[0]) par[0]++;
				gotoxy(currcons,0,y-par[0]);
				continue;
			    case 'd':
				if (par[0]) par[0]--;
				gotoxay(currcons,x,par[0]);
				continue;
			    case 'H': case 'f':
				if (par[0]) par[0]--;
				if (par[1]) par[1]--;
				gotoxay(currcons,par[1],par[0]);
				continue;
			    case 'J':
				csi_J(currcons,par[0]);
				continue;
			    case 'K':
				csi_K(currcons,par[0]);
				continue;
			    case 'L':
				csi_L(currcons,par[0]);
				continue;
			    case 'M':
				csi_M(currcons,par[0]);
				continue;
			    case 'P':
				csi_P(currcons,par[0]);
				continue;
			    case 'c':
				if (!par[0])
					respond_ID(tty);
				continue;
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
				continue;
			    case 'm':
				csi_m(currcons);
				continue;
			    case 'q': /* DECLL - but only 3 leds */
				/* map 0,1,2,3 to 0,1,2,4 */
				if (par[0] < 4)
					setledstate(kbd_table + currcons,
						    (par[0] < 3) ? par[0] : 4);
				continue;
			    case 'r':
				if (!par[0])
					par[0]++;
				if (!par[1])
					par[1] = rows;
				/* Minimum allowed region is 2 lines */
				if (par[0] < par[1] &&
				    par[1] <= rows) {
					top=par[0]-1;
					bottom=par[1];
					gotoxay(currcons,0,0);
				}
				continue;
			    case 's':
				save_cur(currcons);
				continue;
			    case 'u':
				restore_cur(currcons);
				continue;
			    case 'X':
				csi_X(currcons, par[0]);
				continue;
			    case '@':
				csi_at(currcons,par[0]);
				continue;
			    case ']': /* setterm functions */
				setterm_command(currcons);
				continue;
			}
			continue;
		    case ESpercent:
			vc_state = ESnormal;
			switch (c) {
			    case '@':  /* defined in ISO 2022 */
				utf = 0;
				continue;
			    case 'G':  /* prelim official escape code */
			    case '8':  /* retained for compatibility */
				utf = 1;
				continue;
			}
			continue;
		    case ESfunckey:
			vc_state = ESnormal;
			continue;
		    case EShash:
			vc_state = ESnormal;
			if (c == '8') {
				/* DEC screen alignment test. kludge :-) */
				video_erase_char =
					(video_erase_char & 0xff00) | 'E';
				/* Arno:
				 * Doesn't work, because csi_J(c,2)
				 * calls con_clear and doesn't print
				 * the erase char..
				 */
				csi_J(currcons, 2);
				video_erase_char =
					(video_erase_char & 0xff00) | ' ';
			}
			continue;
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
			continue;
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
			continue;
		    default:
			vc_state = ESnormal;
		}
	}
	if (vcmode != KD_GRAPHICS)
		set_cursor(currcons);
	enable_bh(CONSOLE_BH);
	return n;
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

void poke_blanked_console(void)
{
	timer_active &= ~(1<<BLANK_TIMER);
	if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
		return;
	if (console_blanked) {
		timer_table[BLANK_TIMER].fn = unblank_screen;
		timer_table[BLANK_TIMER].expires = 0;
		timer_active |= 1<<BLANK_TIMER;
	} else if (blankinterval) {
		timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
		timer_active |= 1<<BLANK_TIMER;
	}
}

/* DPC: New version of console_print using putcs */

void console_print(const char * b)
{
   int currcons = fg_console;
   unsigned char c;
   const char *start = b;
   ushort count      = 0;
   ushort myx        = x;
   static int printing = 0;

   if (!printable || printing)
	   return;	 /* console not yet initialized */
   printing = 1;

   if (kmsg_redirect && vc_cons_allocated(kmsg_redirect - 1))
	   currcons = kmsg_redirect - 1;

   if (!vc_cons_allocated(currcons)) {
	   /* impossible */
	   printk("console_print: tty %d not allocated ??\n", currcons+1);
	   printing = 0;
	   return;
   }

   /* undraw cursor first */
   hide_cursor(currcons);

   /* Contrived structure to try to emulate original need_wrap behaviour
    * Problems caused when we have need_wrap set on '\n' character */
   
   while ((c = *(b++)) != 0) {
       if (c == 10 || c == 13 || c == 8 || need_wrap) {
           if ((count = b - start - 1) > 0) {
               sw->con_putcs(vc_cons[currcons].d, start, count ,
                             y, x);
               x += count;
	       if (need_wrap)
		 x--;
           }

	   if (c == 8) {	/* backspace */
	       bs(currcons);
	       start = b;
	       myx = x;
	       continue;
	   }
           if (c != 13)
               lf(currcons);
           cr(currcons);

           if (c == 10 || c == 13) {
               start = b; myx = x; continue;
           }

           start = b-1; myx = x;
       }

       *pos = c | (attr << 8);
       if (myx == cols - 1) {
           need_wrap = 1;
           continue;
       }
       pos++;
       myx++;
   }

   if ((count = b - start -1) > 0) {
       sw->con_putcs(vc_cons[currcons].d, start, count ,
                     y, x);
       x += count;
       if (x == cols)
	 {
	   x--;
           need_wrap = 1;
	 }
   }
   
   set_cursor(currcons);
   poke_blanked_console();
   printing = 0;
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

static void vc_init(unsigned int currcons, int do_clear)
{
	long base = (long) vc_scrbuf[currcons];

	pos = (unsigned short *)(origin = (ulong)video_mem_start = base);
	scr_end = base + screenbuf_size;
	video_mem_end = base + screenbuf_size;
	reset_vc(currcons);
	def_color       = 0x07;   /* white */
	ulcolor		= 0x0f;   /* bold white */
	halfcolor       = 0x08;   /* grey */
	vt_cons[currcons]->paste_wait = 0;
	reset_terminal(currcons, do_clear);
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
	if (want_console >= 0) {
		if (want_console != fg_console) {
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
}

/*
 *  unsigned long con_init(unsigned long);
 *
 * This routine initializes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequence.
 *
 * Reads the information preserved by setup.s to determine the current display
 * type and sets everything accordingly.
 */
unsigned long con_init(unsigned long kmem_start)
{
	char *display_desc = "????";
	unsigned int currcons = 0;
	extern int serial_debug;

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
	console_driver.chars_in_buffer = con_chars_in_buffer;
	console_driver.ioctl = vt_ioctl;
	console_driver.stop = con_stop;
	console_driver.start = con_start;
	console_driver.throttle = con_throttle;
	console_driver.unthrottle = con_unthrottle;
	
	if (tty_register_driver(&console_driver))
		panic("Couldn't register console driver\n");
	
	kmem_start = conswitchp->con_startup (kmem_start, &display_desc);

	timer_table[BLANK_TIMER].fn = blank_screen;
	timer_table[BLANK_TIMER].expires = 0;
	if (blankinterval) {
		timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
		timer_active |= 1<<BLANK_TIMER;
	}

	/* Due to kmalloc roundup allocating statically is more efficient -
	   so provide MIN_NR_CONSOLES for people with very little memory */
	for (currcons = 0; currcons < MIN_NR_CONSOLES; currcons++) {
		vc_cons[currcons].d = (struct vc_data *) kmem_start;
		kmem_start += sizeof(struct vc_data);
		vt_cons[currcons] = (struct vt_struct *) kmem_start;
		kmem_start += sizeof(struct vt_struct);

		/* ++Geert: sw->con_init determines console size */
		sw = conswitchp;
		cons_num = currcons;
		sw->con_init (vc_cons[currcons].d);
		size_row = cols<<1;
		screenbuf_size = rows*size_row;

		vc_scrbuf[currcons] = (unsigned short *) kmem_start;
		kmem_start += screenbuf_size;
		kmalloced = 0;
		vc_init(currcons, currcons);
	}

	currcons = fg_console = 0;

	gotoxy(currcons,0,0);
	csi_J(currcons, 0);
	printable = 1;
	update_screen(fg_console);
	sw->con_cursor(vc_cons[currcons].d, CM_DRAW);
	printable = 1;

	/* If "serdebug" cmd line option was present, don't register for printk */
	if (!serial_debug)
		register_console(console_print);
	printk("Console: %s %s %ldx%ld, %d virtual console%s (max %d)\n",
		can_do_color ? "colour":"mono",
		display_desc,
		cols,rows,
		MIN_NR_CONSOLES, (MIN_NR_CONSOLES == 1) ? "" : "s", MAX_NR_CONSOLES);

	init_bh(CONSOLE_BH, console_bh);
	return kmem_start;
}

void vesa_powerdown_screen(void)
{
	int currcons = fg_console;

	timer_active &= ~(1<<BLANK_TIMER);
	timer_table[BLANK_TIMER].fn = unblank_screen;

	/* Power down if currently suspended (1 or 2),
	 * suspend if currently blanked (0),
	 * else do nothing (i.e. already powered down (3)).
	 * Called only if powerdown features are allowed.
	 */
	switch (vesa_blank_mode) {
	case 0:
		sw->con_blank(2);
		break;
	case 1:
	case 2:
		sw->con_blank(4);
		break;
	}
}

void do_blank_screen(int nopowersave)
{
	int currcons;

	if (console_blanked)
		return;

	if (!vc_cons_allocated(fg_console)) {
		/* impossible */
		printk("blank_screen: tty %d not allocated ??\n", fg_console+1);
		return;
	}

	/* don't blank graphics */
	if (vt_cons[fg_console]->vc_mode == KD_TEXT) {
		if (vesa_off_interval && !nopowersave) {
			timer_table[BLANK_TIMER].fn = vesa_powerdown_screen;
			timer_table[BLANK_TIMER].expires = jiffies + vesa_off_interval;
			timer_active |= (1<<BLANK_TIMER);
		} else {
			timer_active &= ~(1<<BLANK_TIMER);
			timer_table[BLANK_TIMER].fn = unblank_screen;
		}

		/* try not to lose information by blanking,
		   and not to waste memory */
		currcons = fg_console;
		has_scrolled = 0;
		sw->con_blank(1);
		if (!nopowersave)
			sw->con_blank(vesa_blank_mode + 1);
	}
	else
		hide_cursor(fg_console);
 	console_blanked = fg_console + 1;
}

void do_unblank_screen(void)
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
	if (sw->con_blank (0))
		/* Low-level driver cannot restore -> do it ourselves */
	  	update_screen( fg_console );
	set_cursor (fg_console);
}

void update_screen(int new_console)
{
	int currcons = fg_console;
	int xx, yy, startx, attr_save;
	char buf[256], *bufp;
	unsigned short *p;
	static int lock = 0;

	if (/* new_console == fg_console || */ lock)
		return;
	if (!vc_cons_allocated(new_console)) {
		/* strange ... */
		printk("update_screen: tty %d not allocated ??\n", new_console+1);
		return;
	}
	lock = 1;

	clear_selection();

	currcons = fg_console = new_console;
	sw->con_cursor (vc_cons[currcons].d, CM_ERASE);
	sw->con_switch (vc_cons[new_console].d);
	/* Update the screen contents */
	p = video_mem_start;
	attr_save = attr;
	for (yy = 0; yy < rows; yy++)
	  {
	    bufp = buf;
	    for (startx = xx = 0; xx < cols; xx++)
	      {
		if (attr != ((*p >> 8) & 0xff))
		  {
		    if (bufp > buf)
		      sw->con_putcs (vc_cons[currcons].d, buf, bufp - buf,
				     yy, startx);
		    startx = xx;
		    bufp = buf;
		    attr = (*p >> 8) & 0xff;
		  }
		*bufp++ = *p++;
		if (bufp == buf + sizeof (buf))
		  {
		    sw->con_putcs (vc_cons[currcons].d, buf, bufp - buf,
				   yy, startx);
		    startx = xx + 1;
		    bufp = buf;
		  }
	      }
	    if (bufp > buf)
	      sw->con_putcs (vc_cons[currcons].d, buf, bufp - buf,
			     yy, startx);
	  }
	set_cursor (currcons);
	attr = attr_save;
	set_leds();
	compute_shiftstate();
	lock = 0;
}

/*
 * If a blank_screen is due to a timer, then a power save is allowed.
 * If it is related to console_switching, then avoid vesa_blank().
 */
static void blank_screen(void)
{
	do_blank_screen(0);
}

static void unblank_screen(void)
{
	do_unblank_screen();
}

/*
 * Allocate the console screen memory.
 */
int con_open(struct tty_struct *tty, struct file * filp)
{
	unsigned int currcons;
	int i;

	currcons = MINOR(tty->device) - tty->driver.minor_start;
	
	i = vc_allocate(currcons);
	if (i)
		return i;

	vt_cons[currcons]->vc_num = currcons;
	tty->driver_data = vt_cons[currcons];
	
	if (!tty->winsize.ws_row && !tty->winsize.ws_col) {
		tty->winsize.ws_row = rows;
		tty->winsize.ws_col = cols;
 	}

	return 0;
}

/*
 * PIO_FONT support.
 *
 * Currently we only support 8 pixels wide fonts, at a maximum height
 * of 32 pixels. Userspace fontdata is stored with 32 bytes reserved
 * for each character which is kinda wasty, but this is done in order
 * to maintain compatibility with the EGA/VGA fonts. It is upto the
 * actual low-level console-driver convert data into its favorite
 * format (maybe we should add a `fontoffset' field to the `display'
 * structure so we wont have to convert the fontdata all the time.
 * /Jes
 */

#define cmapsz 8192

static int set_get_font(char * arg, int set, int ch512)
{
#ifdef CAN_LOAD_EGA_FONTS
	int i, unit, size;
	char *charmap;

	if (arg){
		i = verify_area(set ? VERIFY_READ : VERIFY_WRITE,
				(void *)arg, ch512 ? 2*cmapsz : cmapsz);
		if (i)
			return i;
	}else
		return -EINVAL;


	size = ch512 ? 2*cmapsz : cmapsz;

	charmap = (char *)kmalloc(size, GFP_USER);

	if (set){
		memcpy_fromfs(charmap, arg, size);

		for (unit = 32; unit > 0; unit--)
			for (i = 0; i < (ch512 ? 512 : 256); i++)
				if (charmap[32*i+unit-1])
					goto nonzero;
	nonzero:
		i = conswitchp->con_set_font(vc_cons[fg_console].d, 8,
					     unit, charmap);
	}else{
		memset(charmap, 0, size);
		i = conswitchp->con_get_font(vc_cons[fg_console].d,
					     &unit, &unit, charmap);
		memcpy_tofs(arg, charmap, size);
	}
	kfree(charmap);

	return i;
#else
	return -EINVAL;
#endif
}

/*
 * Load palette into the EGA/VGA DAC registers. arg points to a colour
 * map, 3 bytes per colour, 16 colours, range from 0 to 255.
 */

int con_set_cmap (unsigned char *arg)
{
	return -EINVAL;
}

int con_get_cmap (unsigned char *arg)
{
	return -EINVAL;
}

void reset_palette(int currcons)
{
}

void set_palette(void)
{
}

/*
 * Load font into the EGA/VGA character generator. arg points to a 8192
 * byte map, 32 bytes per character. Only first H of them are used for
 * 8xH fonts (0 < H <= 32).
 */

int con_set_font (char *arg, int ch512)
{
	int i;

	i = set_get_font (arg,1,ch512);
  	if ( !i ) {
		hashtable_contents_valid = 0;
      		video_mode_512ch = ch512;
      		console_charmask = ch512 ? 0x1ff : 0x0ff;
	}
	return i;
}

int con_get_font (char *arg)
{
	return set_get_font (arg,0,video_mode_512ch);
}

/*
 * Adjust the screen to fit a font of a certain height
 *
 * Returns < 0 for error, 0 if nothing changed, and the number
 * of lines on the adjusted console if changed.
 */
int con_adjust_height(unsigned long fontheight)
{
	return -EINVAL;
}

void set_vesa_blanking(int arg)
{
	char *argp = (char *)arg + 1;
	unsigned int mode = get_fs_byte(argp);
	vesa_blank_mode = (mode < 4) ? mode : 0;
}

unsigned long get_video_num_lines(unsigned int currcons)
{
	return(rows);
}

unsigned long get_video_num_columns(unsigned int currcons)
{
	return(cols);
}

unsigned long get_video_size_row(unsigned int currcons)
{
	return(size_row);
}
