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
 *     'void mouse_report(struct tty_struct * tty, int butt, int mrx, int mry)'
 *     'int mouse_reporting(void)'
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
 */

#define BLANK 0x0020

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
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif

#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>

#include "kbd_kern.h"
#include "vt_kern.h"
#include "consolemap.h"
#include "selection.h"
#include "console_struct.h"

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

struct tty_driver console_driver;
static int console_refcount;
static struct tty_struct *console_table[MAX_NR_CONSOLES];
static struct termios *console_termios[MAX_NR_CONSOLES];
static struct termios *console_termios_locked[MAX_NR_CONSOLES];
unsigned short *vc_scrbuf[MAX_NR_CONSOLES];
struct vc vc_cons [MAX_NR_CONSOLES];

static void con_setsize(unsigned long rows, unsigned long cols);
static void vc_init(unsigned int console, unsigned long rows,
		    unsigned long cols, int do_clear);
extern void get_scrmem(int currcons);
extern void set_scrmem(int currcons, long offset);
static void set_origin(int currcons);
static void blank_screen(void);
static void unblank_screen(void);
extern void change_console(unsigned int);
extern void poke_blanked_console(void);
static void gotoxy(int currcons, int new_x, int new_y);
static void save_cur(int currcons);
extern void set_cursor(int currcons);
extern void hide_cursor(void);
static void reset_terminal(int currcons, int do_clear);
extern void reset_vc(unsigned int new_console);
extern void vt_init(void);
extern void register_console(void (*proc)(const char *));
extern void vesa_blank(void);
extern void vesa_unblank(void);
extern void vesa_powerdown(void);
extern void compute_shiftstate(void);
extern void reset_palette(int currcons);
extern void set_palette(void);
extern unsigned long con_type_init(unsigned long, const char **);
extern int set_get_cmap(unsigned char *, int);
extern int set_get_font(unsigned char *, int, int);

/* Description of the hardware situation */
unsigned char	video_type;		/* Type of display being used	*/
unsigned long	video_mem_base;		/* Base of video memory		*/
unsigned long	video_mem_term;		/* End of video memory		*/
unsigned short	video_port_reg;		/* Video register select port	*/
unsigned short	video_port_val;		/* Video register value port	*/
unsigned long	video_num_columns;	/* Number of text columns	*/
unsigned long	video_num_lines;	/* Number of text lines		*/
unsigned long	video_size_row;
unsigned long	video_screen_size;

int can_do_color = 0;
static int printable = 0;		/* Is console ready for printing? */

int		video_mode_512ch = 0;	/* 512-character mode */
unsigned long	video_font_height;	/* Height of current screen font */
unsigned long	video_scan_lines;	/* Number of scan lines on screen */
unsigned long   default_font_height;    /* Height of default screen font */
int		video_font_is_default = 1;
static unsigned short console_charmask = 0x0ff;

/* used by kbd_bh - set by keyboard_interrupt */
       int do_poke_blanked_console = 0;
       int console_blanked = 0;
static int blankinterval = 10*60*HZ;
static int vesa_off_interval = 0;
static long blank_origin, blank__origin, unblank_origin;


#ifdef CONFIG_SERIAL_ECHO

#include <linux/serial_reg.h>

extern int serial_echo_init (int base);
extern int serial_echo_print (const char *s);

/*
 * this defines the address for the port to which printk echoing is done
 *  when CONFIG_SERIAL_ECHO is defined
 */
#define SERIAL_ECHO_PORT	0x3f8	/* COM1 */

static int serial_echo_port = 0;

#define serial_echo_outb(v,a) outb((v),(a)+serial_echo_port)
#define serial_echo_inb(a)    inb((a)+serial_echo_port)

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

/* Wait for transmitter & holding register to empty */
#define WAIT_FOR_XMITR \
 do { \
       lsr = serial_echo_inb(UART_LSR); \
 } while ((lsr & BOTH_EMPTY) != BOTH_EMPTY)

/* These two functions abstract the actual communications with the
 * debug port.	This is so we can change the underlying communications
 * mechanism without modifying the rest of the code.
 */
int
serial_echo_print(const char *s)
{
	int     lsr, ier;
	int     i;

	if (!serial_echo_port) return (0);

	/*
	 * First save the IER then disable the interrupts
	 */
	ier = serial_echo_inb(UART_IER);
	serial_echo_outb(0x00, UART_IER);

	/*
	 * Now, do each character
	 */
	for (i = 0; *s; i++, s++) {
		WAIT_FOR_XMITR;

		/* Send the character out. */
		serial_echo_outb(*s, UART_TX);

		/* if a LF, also do CR... */
		if (*s == 10) {
			WAIT_FOR_XMITR;
			serial_echo_outb(13, UART_TX);
		}
	}

	/*
	 * Finally, Wait for transmitter & holding register to empty
	 *  and restore the IER
	 */
	do {
		lsr = serial_echo_inb(UART_LSR);
	} while ((lsr & BOTH_EMPTY) != BOTH_EMPTY);
	serial_echo_outb(ier, UART_IER);

	return (0);
}


int
serial_echo_init(int base)
{
	int comstat, hi, lo;
	
	if (base != 0x2f8 && base != 0x3f8) {
		serial_echo_port = 0;
		return (0);
	} else
	  serial_echo_port = base;

	/*
	 * read the Divisor Latch
	 */
	comstat = serial_echo_inb(UART_LCR);
	serial_echo_outb(comstat | UART_LCR_DLAB, UART_LCR);
	hi = serial_echo_inb(UART_DLM);
	lo = serial_echo_inb(UART_DLL);
	serial_echo_outb(comstat, UART_LCR);

	/*
	 * now do hardwired init
	 */
	serial_echo_outb(0x03, UART_LCR); /* No parity, 8 data bits, 1 stop */
	serial_echo_outb(0x83, UART_LCR); /* Access divisor latch */
	serial_echo_outb(0x00, UART_DLM); /* 9600 baud */
	serial_echo_outb(0x0c, UART_DLL);
	serial_echo_outb(0x03, UART_LCR); /* Done with divisor */

	/* Prior to disabling interrupts, read the LSR and RBR
	 * registers
	 */
	comstat = serial_echo_inb(UART_LSR); /* COM? LSR */
	comstat = serial_echo_inb(UART_RX);	/* COM? RBR */
	serial_echo_outb(0x00, UART_IER); /* Disable all interrupts */

	return(0);
}

#endif /* CONFIG_SERIAL_ECHO */


int vc_cons_allocated(unsigned int i)
{
	return (i < MAX_NR_CONSOLES && vc_cons[i].d);
}

int vc_allocate(unsigned int i)		/* return 0 on success */
{
	if (i >= MAX_NR_CONSOLES)
	  return -ENXIO;
	if (!vc_cons[i].d) {
	    long p, q;

	    /* prevent users from taking too much memory */
	    if (i >= MAX_NR_USER_CONSOLES && !suser())
	      return -EPERM;

	    /* due to the granularity of kmalloc, we waste some memory here */
	    /* the alloc is done in two steps, to optimize the common situation
	       of a 25x80 console (structsize=216, video_screen_size=4000) */
	    q = (long) kmalloc(video_screen_size, GFP_KERNEL);
	    if (!q)
	      return -ENOMEM;
	    p = (long) kmalloc(structsize, GFP_KERNEL);
	    if (!p) {
		kfree_s((char *) q, video_screen_size);
		return -ENOMEM;
	    }

	    vc_cons[i].d = (struct vc_data *) p;
	    p += sizeof(struct vc_data);
	    vt_cons[i] = (struct vt_struct *) p;
	    vc_scrbuf[i] = (unsigned short *) q;
	    vc_cons[i].d->vc_kmalloced = 1;
	    vc_cons[i].d->vc_screenbuf_size = video_screen_size;
	    vc_init (i, video_num_lines, video_num_columns, 1);
	}
	return 0;
}

/*
 * Change # of rows and columns (0 means unchanged)
 * [this is to be used together with some user program
 * like resize that changes the hardware videomode]
 */
int vc_resize(unsigned long lines, unsigned long cols)
{
	unsigned long cc, ll, ss, sr;
	unsigned long occ, oll, oss, osr;
	unsigned short *p;
	unsigned int currcons, i;
	unsigned short *newscreens[MAX_NR_CONSOLES];
	long ol, nl, rlth, rrem;

	cc = (cols ? cols : video_num_columns);
	ll = (lines ? lines : video_num_lines);
	sr = cc << 1;
	ss = sr * ll;

	if (ss > video_mem_term - video_mem_base)
	  return -ENOMEM;

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

	get_scrmem(fg_console);

	oll = video_num_lines;
	occ = video_num_columns;
	osr = video_size_row;
	oss = video_screen_size;

	video_num_lines = ll;
	video_num_columns = cc;
	video_size_row = sr;
	video_screen_size = ss;

	for (currcons = 0; currcons < MAX_NR_CONSOLES; currcons++) {
	    if (!vc_cons_allocated(currcons))
	      continue;

	    rlth = MIN(osr, sr);
	    rrem = sr - rlth;
	    ol = origin;
	    nl = (long) newscreens[currcons];
	    if (ll < oll)
	      ol += (oll - ll) * osr;

	    while (ol < scr_end) {
		memcpyw((unsigned short *) nl, (unsigned short *) ol, rlth);
		if (rrem)
		  memsetw((void *)(nl + rlth), video_erase_char, rrem);
		ol += osr;
		nl += sr;
	    }

	    if (kmalloced)
	      kfree_s(vc_scrbuf[currcons], screenbuf_size);
	    vc_scrbuf[currcons] = newscreens[currcons];
	    kmalloced = 1;
	    screenbuf_size = ss;

	    origin = video_mem_start = (long) vc_scrbuf[currcons];
	    scr_end = video_mem_end = video_mem_start + ss;

	    if (scr_end > nl)
	      memsetw((void *) nl, video_erase_char, scr_end - nl);

	    /* do part of a reset_terminal() */
	    top = 0;
	    bottom = video_num_lines;
	    gotoxy(currcons, x, y);
	    save_cur(currcons);
	}

	set_scrmem(fg_console, 0);
	set_origin(fg_console);
	set_cursor(fg_console);

	return 0;
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
	int max_y;

	if (new_x < 0)
		x = 0;
	else
		if (new_x >= video_num_columns)
			x = video_num_columns - 1;
		else
			x = new_x;
 	if (decom) {
		new_y += top;
		max_y = bottom;
	} else
		max_y = video_num_lines;
	if (new_y < 0)
		y = 0;
	else
		if (new_y >= max_y)
			y = max_y - 1;
		else
			y = new_y;
	pos = origin + y*video_size_row + (x<<1);
	need_wrap = 0;
}

/*
 * Hardware scrollback support
 */
extern void __set_origin(unsigned short);
unsigned short __real_origin;       /* offset of non-scrolled screen */
unsigned short __origin;	    /* offset of currently displayed screen */
unsigned char has_wrapped;          /* all of videomem is data of fg_console */
static unsigned char hardscroll_enabled;
static unsigned char hardscroll_disabled_by_init = 0;

void no_scroll(char *str, int *ints)
{
  /*
   * Disabling scrollback is required for the Braillex ib80-piezo
   * Braille reader made by F.H. Papenmeier (Germany).
   * Use the "no-scroll" bootflag.
   */
	hardscroll_disabled_by_init = 1;
	hardscroll_enabled = 0;
}

static void scrolldelta(int lines)
{
	int new_origin;
	int last_origin_rel = (((video_mem_term - video_mem_base)
         / video_num_columns / 2) - (video_num_lines - 1)) * video_num_columns;

	new_origin = __origin + lines * video_num_columns;
	if (__origin > __real_origin)
		new_origin -= last_origin_rel;
	if (new_origin < 0) {
		int s_top = __real_origin + video_num_lines*video_num_columns;
		new_origin += last_origin_rel;
		if (new_origin < s_top)
			new_origin = s_top;
		if (new_origin > last_origin_rel - video_num_columns
		    || has_wrapped == 0)
			new_origin = 0;
		else {
			unsigned short * d = (unsigned short *) video_mem_base;
			unsigned short * s = d + last_origin_rel;
			int count = (video_num_lines-1)*video_num_columns;
			while (count) {
				count--;
				scr_writew(scr_readw(d++),s++);
			}
		}
	} else if (new_origin > __real_origin)
		new_origin = __real_origin;

	__set_origin(new_origin);
}

void scrollback(int lines)
{
	if (!lines)
		lines = video_num_lines/2;
	scrolldelta(-lines);
}

void scrollfront(int lines)
{
	if (!lines)
		lines = video_num_lines/2;
	scrolldelta(lines);
}

static void set_origin(int currcons)
{
	if (video_type != VIDEO_TYPE_EGAC && video_type != VIDEO_TYPE_VGAC
	    && video_type != VIDEO_TYPE_EGAM)
		return;
	if (currcons != fg_console || console_blanked || vcmode == KD_GRAPHICS)
		return;
	__real_origin = (origin-video_mem_base) >> 1;
	__set_origin(__real_origin);
}

void scrup(int currcons, unsigned int t, unsigned int b)
{
	int hardscroll = hardscroll_enabled;

	if (b > video_num_lines || t >= b)
		return;
	if (t || b != video_num_lines)
		hardscroll = 0;
	if (hardscroll) {
		origin += video_size_row;
		pos += video_size_row;
		scr_end += video_size_row;
		if (scr_end > video_mem_end) {
			unsigned short * d = (unsigned short *) video_mem_start;
			unsigned short * s = (unsigned short *) origin;
			unsigned int count;

			count = (video_num_lines-1)*video_num_columns;
			while (count) {
				count--;
				scr_writew(scr_readw(s++),d++);
			}
			count = video_num_columns;
			while (count) {
				count--;
				scr_writew(video_erase_char, d++);
			}
			scr_end -= origin-video_mem_start;
			pos -= origin-video_mem_start;
			origin = video_mem_start;
			has_scrolled = 1;
			if (currcons == fg_console)
				has_wrapped = 1;
		} else {
			unsigned short * d;
			unsigned int count;

			d = (unsigned short *) (scr_end - video_size_row);
			count = video_num_columns;
			while (count) {
				count--;
				scr_writew(video_erase_char, d++);
			}
		}
		set_origin(currcons);
	} else {
		unsigned short * d = (unsigned short *) (origin+video_size_row*t);
		unsigned short * s = (unsigned short *) (origin+video_size_row*(t+1));
		unsigned int count = (b-t-1) * video_num_columns;

		while (count) {
			count--;
			scr_writew(scr_readw(s++), d++);
		}
		count = video_num_columns;
		while (count) {
			count--;
			scr_writew(video_erase_char, d++);
		}
	}
}

void
scrdown(int currcons, unsigned int t, unsigned int b)
{
	unsigned short *d, *s;
	unsigned int count;

	if (b > video_num_lines || t >= b)
		return;
	d = (unsigned short *) (origin+video_size_row*b);
	s = (unsigned short *) (origin+video_size_row*(b-1));
	count = (b-t-1)*video_num_columns;
	while (count) {
		count--;
		scr_writew(scr_readw(--s), --d);
	}
	count = video_num_columns;
	while (count) {
		count--;
		scr_writew(video_erase_char, --d);
	}
	has_scrolled = 1;
}

static void lf(int currcons)
{
    	/* don't scroll if above bottom of scrolling region, or
	 * if below scrolling region
	 */
    	if (y+1 == bottom)
		scrup(currcons,top,bottom);
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
		scrdown(currcons,top,bottom);
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
	unsigned long count;
	unsigned short * start;

	switch (vpar) {
		case 0:	/* erase from cursor to end of display */
			count = (scr_end-pos)>>1;
			start = (unsigned short *) pos;
			break;
		case 1:	/* erase from start to cursor */
			count = ((pos-origin)>>1)+1;
			start = (unsigned short *) origin;
			break;
		case 2: /* erase whole display */
			count = video_num_columns * video_num_lines;
			start = (unsigned short *) origin;
			break;
		default:
			return;
	}
	while (count) {
		count--;
		scr_writew(video_erase_char, start++);
	}
	need_wrap = 0;
}

static void csi_K(int currcons, int vpar)
{
	unsigned long count;
	unsigned short * start;

	switch (vpar) {
		case 0:	/* erase from cursor to end of line */
			count = video_num_columns-x;
			start = (unsigned short *) pos;
			break;
		case 1:	/* erase from start of line to cursor */
			start = (unsigned short *) (pos - (x<<1));
			count = x+1;
			break;
		case 2: /* erase whole line */
			start = (unsigned short *) (pos - (x<<1));
			count = video_num_columns;
			break;
		default:
			return;
	}
	while (count) {
		count--;
		scr_writew(video_erase_char, start++);
	}
	need_wrap = 0;
}

static void csi_X(int currcons, int vpar) /* erase the following vpar positions */
{					  /* not vt100? */
	unsigned long count;
	unsigned short * start;

	if (!vpar)
		vpar++;

	start = (unsigned short *) pos;
	count = (vpar > video_num_columns-x) ? (video_num_columns-x) : vpar;

	while (count) {
		count--;
		scr_writew(video_erase_char, start++);
	}
	need_wrap = 0;
}

static void update_attr(int currcons)
{
	attr = color;
	if (can_do_color) {
		if (underline)
			attr = (attr & 0xf0) | ulcolor;
		else if (intensity == 0)
			attr = (attr & 0xf0) | halfcolor;
	}
	if (reverse ^ decscnm)
		attr = reverse_video_char(attr);
	if (blink)
		attr ^= 0x80;
	if (intensity == 2)
		attr ^= 0x08;
	if (!can_do_color) {
		if (underline)
			attr = (attr & 0xf8) | 0x01;
		else if (intensity == 0)
			attr = (attr & 0xf0) | 0x08;
	}
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
	if (viewed && currcons == fg_console)
		p -= (__real_origin - __origin);
	return p;
}

/* Note: inverting the screen twice should revert to the original state */
void invert_screen(int currcons, int offset, int count, int viewed)
{
	unsigned short *p;

	count /= 2;
	p = screenpos(currcons, offset, viewed);
	if (can_do_color)
		while (count--) {
			unsigned short old = scr_readw(p);
			scr_writew(reverse_video_short(old), p);
			p++;
		}
	else
		while (count--) {
			unsigned short old = scr_readw(p);
			scr_writew(old ^ (((old & 0x0700) == 0x0100)
					  ? 0x7000 : 0x7700), p);
			p++;
		}
}

/* used by selection: complement pointer position */
void complement_pos(int currcons, int offset)
{
	static unsigned short *p = NULL;
	static unsigned short old = 0;

	if (p)
		scr_writew(old, p);
	if (offset == -1)
		p = NULL;
	else {
		p = screenpos(currcons, offset, 1);
		old = scr_readw(p);
		scr_writew(old ^ 0x7700, p);
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
				(void) vc_resize(video_num_lines, deccolm ? 132 : 80);
				/* this alone does not suffice; some user mode
				   utility has to change the hardware regs */
#endif
				break;
			case 5:			/* Inverted screen on/off */
				if (decscnm != on_off) {
					decscnm = on_off;
					invert_screen(currcons, 0, video_screen_size, 0);
					update_attr(currcons);
				}
				break;
			case 6:			/* Origin relative/absolute */
				decom = on_off;
				gotoxy(currcons,0,0);
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
	unsigned int i = x;
	unsigned short tmp, old = video_erase_char;
	unsigned short * p = (unsigned short *) pos;

	while (i++ < video_num_columns) {
		tmp = scr_readw(p);
		scr_writew(old, p);
		old = tmp;
		p++;
	}
	need_wrap = 0;
}

static void insert_line(int currcons)
{
	scrdown(currcons,y,bottom);
	need_wrap = 0;
}

static void delete_char(int currcons)
{
	unsigned int i = x;
	unsigned short * p = (unsigned short *) pos;

	while (++i < video_num_columns) {
		scr_writew(scr_readw(p+1), p);
		p++;
	}
	scr_writew(video_erase_char, p);
	need_wrap = 0;
}

static void delete_line(int currcons)
{
	scrup(currcons,y,bottom);
	need_wrap = 0;
}

static void csi_at(int currcons, unsigned int nr)
{
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_char(currcons);
}

static void csi_L(int currcons, unsigned int nr)
{
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_line(currcons);
}

static void csi_P(int currcons, unsigned int nr)
{
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		delete_char(currcons);
}

static void csi_M(int currcons, unsigned int nr)
{
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr=1;
	while (nr--)
		delete_line(currcons);
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

	if (currcons == sel_cons)
		clear_selection();

	disable_bh(CONSOLE_BH);
	while (!tty->stopped &&	count) {
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

		/* If the original code was < 32 we only allow a
		 * glyph to be displayed if the code is not normally
		 * used (such as for cursor movement) or if the
		 * disp_ctrl mode has been explicitly enabled.
		 * Note: ESC is *never* allowed to be displayed as
		 * that would disable all escape sequences!
		 * To display font position 0x1B, go into UTF mode
		 * and display character U+F01B, or change the mapping.
		 */
		ok = (tc && (c >= 32 || (!utf && !(((disp_ctrl ? CTRL_ALWAYS
					    : CTRL_ACTION) >> c) & 1))));

		if (vc_state == ESnormal && ok) {
			/* Now try to find out how to display it */
			tc = conv_uni_to_pc(tc);
			if ( tc == -4 )
			  {
			    /* If we got -4 (not found) then see if we have
			       defined a replacement character (U+FFFD) */
			    tc = conv_uni_to_pc(0xfffd);
			  }
			else if ( tc == -3 )
			  {
			    /* Bad hash table -- hope for the best */
			    tc = c;
			  }
			if (tc & ~console_charmask)
			  continue; /* Conversion failed */

			if (need_wrap) {
				cr(currcons);
				lf(currcons);
			}
			if (decim)
				insert_char(currcons);
			scr_writew( video_mode_512ch ?
			   ((attr & 0xf7) << 8) + ((tc & 0x100) << 3) +
			   (tc & 0x0ff) : (attr << 8) + tc,
			   (unsigned short *) pos);
			if (x == video_num_columns - 1)
				need_wrap = decawm;
			else {
				x++;
				pos+=2;
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
				pos -= (x << 1);
				while (x < video_num_columns - 1) {
					x++;
					if (tab_stop[x >> 5] & (1 << (x & 31)))
						break;
				}
				pos += (x << 1);
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
					reset_palette (currcons);
					vc_state = ESnormal;
				} else
					vc_state = ESnormal;
				continue;
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
						set_palette() ;
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
						gotoxy(currcons,x,par[0]);
						continue;
					case 'H': case 'f':
						if (par[0]) par[0]--;
						if (par[1]) par[1]--;
						gotoxy(currcons,par[1],par[0]);
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
							par[1] = video_num_lines;
						/* Minimum allowed region is 2 lines */
						if (par[0] < par[1] &&
						    par[1] <= video_num_lines) {
							top=par[0]-1;
							bottom=par[1];
							gotoxy(currcons,0,0);
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

void console_print(const char * b)
{
	int currcons = fg_console;
	unsigned char c;
	static int printing = 0;

	if (!printable || printing)
		return;	 /* console not yet initialized */
	printing = 1;

	if (kmsg_redirect && vc_cons_allocated(kmsg_redirect - 1))
		currcons = kmsg_redirect - 1;

	if (!vc_cons_allocated(currcons)) {
		/* impossible */
		printk("console_print: tty %d not allocated ??\n", currcons+1);
		return;
	}

#ifdef CONFIG_SERIAL_ECHO
        serial_echo_print(b);
#endif /* CONFIG_SERIAL_ECHO */

	while ((c = *(b++)) != 0) {
		if (c == 10 || c == 13 || need_wrap) {
			if (c != 13)
				lf(currcons);
			cr(currcons);
			if (c == 10 || c == 13)
				continue;
		}
		if (c == 8) {		/* backspace */
			bs(currcons);
			continue;
		}
		scr_writew((attr << 8) + c, (unsigned short *) pos);
		if (x == video_num_columns - 1) {
			need_wrap = 1;
			continue;
		}
		x++;
		pos+=2;
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

static void vc_init(unsigned int currcons, unsigned long rows, unsigned long cols, int do_clear)
{
	long base = (long) vc_scrbuf[currcons];
	int j, k ;

	video_num_columns = cols;
	video_num_lines = rows;
	video_size_row = cols<<1;
	video_screen_size = video_num_lines * video_size_row;

	pos = origin = video_mem_start = base;
	scr_end = base + video_screen_size;
	video_mem_end = base + video_screen_size;
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

static void con_setsize(unsigned long rows, unsigned long cols)
{
	video_num_lines = rows;
	video_num_columns = cols;
	video_size_row = 2 * cols;
	video_screen_size = video_num_lines * video_size_row;
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
	const char *display_desc = "????";
	int currcons = 0;
	int orig_x = ORIG_X;
	int orig_y = ORIG_Y;

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

	con_setsize(ORIG_VIDEO_LINES, ORIG_VIDEO_COLS);

	timer_table[BLANK_TIMER].fn = blank_screen;
	timer_table[BLANK_TIMER].expires = 0;
	if (blankinterval) {
		timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
		timer_active |= 1<<BLANK_TIMER;
	}

	kmem_start = con_type_init(kmem_start, &display_desc);

	hardscroll_enabled = (hardscroll_disabled_by_init ? 0 :
	  (video_type == VIDEO_TYPE_EGAC
	    || video_type == VIDEO_TYPE_VGAC
	    || video_type == VIDEO_TYPE_EGAM));
	has_wrapped = 0 ;

	/* Due to kmalloc roundup allocating statically is more efficient -
	   so provide MIN_NR_CONSOLES for people with very little memory */
	for (currcons = 0; currcons < MIN_NR_CONSOLES; currcons++) {
		int j, k ;

		vc_cons[currcons].d = (struct vc_data *) kmem_start;
		kmem_start += sizeof(struct vc_data);
		vt_cons[currcons] = (struct vt_struct *) kmem_start;
		kmem_start += sizeof(struct vt_struct);
		vc_scrbuf[currcons] = (unsigned short *) kmem_start;
		kmem_start += video_screen_size;
		kmalloced = 0;
		screenbuf_size = video_screen_size;
       		vc_init(currcons, video_num_lines, video_num_columns, currcons);
		for (j=k=0; j<16; j++) {
			vc_cons[currcons].d->vc_palette[k++] = default_red[j] ;
			vc_cons[currcons].d->vc_palette[k++] = default_grn[j] ;
			vc_cons[currcons].d->vc_palette[k++] = default_blu[j] ;
		}
	}

	currcons = fg_console = 0;

	video_mem_start = video_mem_base;
	video_mem_end = video_mem_term;
	origin = video_mem_start;
	scr_end	= video_mem_start + video_num_lines * video_size_row;
	gotoxy(currcons,orig_x,orig_y);
	set_origin(currcons);
	csi_J(currcons, 0);

	/* Figure out the size of the screen and screen font so we
	   can figure out the appropriate screen size should we load
	   a different font */

	printable = 1;
	if ( video_type == VIDEO_TYPE_VGAC || video_type == VIDEO_TYPE_EGAC
	    || video_type == VIDEO_TYPE_EGAM || video_type == VIDEO_TYPE_TGAC )
	{
		default_font_height = video_font_height = ORIG_VIDEO_POINTS;
		/* This may be suboptimal but is a safe bet - go with it */
		video_scan_lines = video_font_height * video_num_lines;

#ifdef CONFIG_SERIAL_ECHO
		serial_echo_init(SERIAL_ECHO_PORT);
#endif /* CONFIG_SERIAL_ECHO */

		printk("Console: %ld point font, %ld scans\n",
		       video_font_height, video_scan_lines);
	}

	printk("Console: %s %s %ldx%ld, %d virtual console%s (max %d)\n",
		can_do_color ? "colour" : "mono",
		display_desc, video_num_columns, video_num_lines,
		MIN_NR_CONSOLES, (MIN_NR_CONSOLES == 1) ? "" : "s",
	        MAX_NR_CONSOLES);

	/*
	 * can't register TGA yet, because PCI bus probe has *not* taken
	 * place before con_init() gets called. Trigger the real TGA hw
	 * initialization and register_console() event from
	 * within the bus probing code... :-(
	 */
	if (video_type != VIDEO_TYPE_TGAC)
		register_console(console_print);

	init_bh(CONSOLE_BH, console_bh);
	return kmem_start;
}

void vesa_powerdown_screen(void)
{
	timer_active &= ~(1<<BLANK_TIMER);
	timer_table[BLANK_TIMER].fn = unblank_screen;

	vesa_powerdown();
}

void do_blank_screen(int nopowersave)
{
	int currcons;

	if (console_blanked)
		return;

	if(vesa_off_interval && !nopowersave) {
		timer_table[BLANK_TIMER].fn = vesa_powerdown_screen;
		timer_table[BLANK_TIMER].expires = jiffies + vesa_off_interval;
		timer_active |= (1<<BLANK_TIMER);
	} else {
		timer_active &= ~(1<<BLANK_TIMER);
		timer_table[BLANK_TIMER].fn = unblank_screen;
	}

	/* try not to lose information by blanking, and not to waste memory */
	currcons = fg_console;
	has_scrolled = 0;
	blank__origin = __origin;
	blank_origin = origin;
	set_origin(fg_console);
	get_scrmem(fg_console);
	unblank_origin = origin;
	memsetw((void *)blank_origin, BLANK,
		2*video_num_lines*video_num_columns);
	hide_cursor();
	console_blanked = fg_console + 1;

#ifdef CONFIG_APM
	if (apm_display_blank())
		return;
#endif
	if(!nopowersave)
	    vesa_blank();
}

void do_unblank_screen(void)
{
	int currcons;
	int resetorg;
	long offset;

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
	offset = 0;
	resetorg = 0;
	if (console_blanked == fg_console + 1 && origin == unblank_origin
	    && !has_scrolled) {
		/* try to restore the exact situation before blanking */
		resetorg = 1;
		offset = (blank_origin - video_mem_base)
			- (unblank_origin - video_mem_start);
	}

	console_blanked = 0;
	set_scrmem(fg_console, offset);
	set_origin(fg_console);
	set_cursor(fg_console);
	if (resetorg)
		__set_origin(blank__origin);

	vesa_unblank();
#ifdef CONFIG_APM
	if (apm_display_unblank())
		return;
#endif
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

void update_screen(int new_console)
{
	static int lock = 0;

	if (new_console == fg_console || lock)
		return;
	if (!vc_cons_allocated(new_console)) {
		/* strange ... */
		printk("update_screen: tty %d not allocated ??\n", new_console+1);
		return;
	}
	lock = 1;

	clear_selection();

	if (!console_blanked)
		get_scrmem(fg_console);
	else
		console_blanked = -1;	   /* no longer of the form console+1 */
	fg_console = new_console; /* this is the only (nonzero) assignment to fg_console */
				  /* consequently, fg_console will always be allocated */
	set_scrmem(fg_console, 0);
	set_origin(fg_console);
	set_cursor(fg_console);
	set_leds();
	compute_shiftstate();
	lock = 0;
}

/*
 * Allocate the console screen memory.
 */
int con_open(struct tty_struct *tty, struct file * filp)
{
	unsigned int	idx;
	int i;

	idx = MINOR(tty->device) - tty->driver.minor_start;

	i = vc_allocate(idx);
	if (i)
		return i;

	vt_cons[idx]->vc_num = idx;
	tty->driver_data = vt_cons[idx];

	if (!tty->winsize.ws_row && !tty->winsize.ws_col) {
		tty->winsize.ws_row = video_num_lines;
		tty->winsize.ws_col = video_num_columns;
	}
	return 0;
}


/*
 * Load palette into the EGA/VGA DAC registers. arg points to a colour
 * map, 3 bytes per colour, 16 colours, range from 0 to 255.
 */

int con_set_cmap (unsigned char *arg)
{
	return set_get_cmap (arg,1);
}

int con_get_cmap (unsigned char *arg)
{
	return set_get_cmap (arg,0);
}

void reset_palette (int currcons)
{
	int j, k ;
	for (j=k=0; j<16; j++) {
		palette[k++] = default_red[j];
		palette[k++] = default_grn[j];
		palette[k++] = default_blu[j];
	}
	set_palette() ;
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
