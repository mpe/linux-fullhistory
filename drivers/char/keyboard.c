/*
 * linux/drivers/char/keyboard.c
 *
 * Keyboard driver for Linux v0.99 using Latin-1.
 *
 * Written for linux by Johan Myreen as a translation from
 * the assembly version by Linus (with diacriticals added)
 *
 * Some additional features added by Christoph Niemann (ChN), March 1993
 *
 * Loadable keymaps by Risto Kankkunen, May 1993
 *
 * Diacriticals redone & other small changes, aeb@cwi.nl, June 1993
 * Added decr/incr_console, dynamic keymaps, Unicode support,
 * dynamic function/string keys, led setting,  Sept 1994
 * 
 */

#define KEYBOARD_IRQ 1

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/config.h>
#include <linux/signal.h>
#include <linux/string.h>

#include <asm/bitops.h>

#include "kbd_kern.h"
#include "diacr.h"
#include "vt_kern.h"

#define SIZE(x) (sizeof(x)/sizeof((x)[0]))

#define KBD_REPORT_ERR
#define KBD_REPORT_UNKN
/* #define KBD_IS_FOCUS_9000 */

#ifndef KBD_DEFMODE
#define KBD_DEFMODE ((1 << VC_REPEAT) | (1 << VC_META))
#endif

#ifndef KBD_DEFLEDS
/*
 * Some laptops take the 789uiojklm,. keys as number pad when NumLock
 * is on. This seems a good reason to start with NumLock off.
 */
#define KBD_DEFLEDS 0
#endif

#ifndef KBD_DEFLOCK
#define KBD_DEFLOCK 0
#endif

/*
 * The default IO slowdown is doing 'inb()'s from 0x61, which should be
 * safe. But as that is the keyboard controller chip address, we do our
 * slowdowns here by doing short jumps: the keyboard controller should
 * be able to keep up
 */
#define REALLY_SLOW_IO
#define SLOW_IO_BY_JUMPING
#include <asm/io.h>
#include <asm/system.h>

extern void poke_blanked_console(void);
extern void ctrl_alt_del(void);
extern void reset_vc(unsigned int new_console);
extern void change_console(unsigned int new_console);
extern void scrollback(int);
extern void scrollfront(int);
extern int vc_cons_allocated(unsigned int);

#define fake_keyboard_interrupt() \
__asm__ __volatile__("int $0x21")

unsigned char kbd_read_mask = 0x01;	/* modified by psaux.c */

/*
 * global state includes the following, and various static variables
 * in this module: prev_scancode, shift_state, diacr, npadch,
 *   dead_key_next, last_console
 */

/* shift state counters.. */
static unsigned char k_down[NR_SHIFT] = {0, };
/* keyboard key bitmap */
static unsigned long key_down[8] = { 0, };

static int want_console = -1;
static int last_console = 0;		/* last used VC */
static int dead_key_next = 0;
/* 
 * In order to retrieve the shift_state (for the mouse server), either
 * the variable must be global, or a new procedure must be created to 
 * return the value. I chose the former way.
 */
/*static*/ int shift_state = 0;
static int npadch = -1;			/* -1 or number assembled on pad */
static unsigned char diacr = 0;
static char rep = 0;			/* flag telling character repeat */
struct kbd_struct kbd_table[MAX_NR_CONSOLES];
static struct tty_struct **ttytab;
static struct kbd_struct * kbd = kbd_table;
static struct tty_struct * tty = NULL;

/* used only by send_data - set by keyboard_interrupt */
static volatile unsigned char reply_expected = 0;
static volatile unsigned char acknowledge = 0;
static volatile unsigned char resend = 0;

typedef void (*k_hand)(unsigned char value, char up_flag);
typedef void (k_handfn)(unsigned char value, char up_flag);

static k_handfn
	do_self, do_fn, do_spec, do_pad, do_dead, do_cons, do_cur, do_shift,
	do_meta, do_ascii, do_lock, do_lowercase, do_ignore;

static k_hand key_handler[16] = {
	do_self, do_fn, do_spec, do_pad, do_dead, do_cons, do_cur, do_shift,
	do_meta, do_ascii, do_lock, do_lowercase,
	do_ignore, do_ignore, do_ignore, do_ignore
};

typedef void (*void_fnp)(void);
typedef void (void_fn)(void);

static void_fn enter, show_ptregs, send_intr, lastcons, caps_toggle,
	num, hold, scroll_forw, scroll_back, boot_it, caps_on, compose,
	SAK, decr_console, incr_console;

static void_fnp spec_fn_table[] = {
	NULL,		enter,		show_ptregs,	show_mem,
	show_state,	send_intr,	lastcons,	caps_toggle,
	num,		hold,		scroll_forw,	scroll_back,
	boot_it,	caps_on,	compose,	SAK,
	decr_console,	incr_console
};

/* maximum values each key_handler can handle */
const int max_vals[] = {
	255, SIZE(func_table) - 1, SIZE(spec_fn_table) - 1, NR_PAD - 1,
	NR_DEAD - 1, 255, 3, NR_SHIFT - 1,
	255, NR_ASCII - 1, NR_LOCK - 1, 255
};

const int NR_TYPES = SIZE(max_vals);

static void put_queue(int);
static unsigned char handle_diacr(unsigned char);

/* pt_regs - set by keyboard_interrupt(), used by show_ptregs() */
static struct pt_regs * pt_regs;

static inline void kb_wait(void)
{
	int i;

	for (i=0; i<0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

static inline void send_cmd(unsigned char c)
{
	kb_wait();
	outb(c,0x64);
}

/*
 * Many other routines do put_queue, but I think either
 * they produce ASCII, or they produce some user-assigned
 * string, and in both cases we might assume that it is
 * in utf-8 already.
 */
void to_utf8(ushort c) {
    if (c < 0x80)
	put_queue(c);			/*  0*******  */
    else if (c < 0x800) {
	put_queue(0xc0 | (c >> 6)); 	/*  110***** 10******  */
	put_queue(0x80 | (c & 0x3f));
    } else {
	put_queue(0xe0 | (c >> 12)); 	/*  1110**** 10****** 10******  */
	put_queue(0x80 | ((c >> 6) & 0x3f));
	put_queue(0x80 | (c & 0x3f));
    }
    /* uft-8 is defined for words of up to 36 bits,
       but we need only 16 bits here */
}

/*
 * Translation of escaped scancodes to keysyms.
 * This should be user-settable.
 */
#define E0_BASE 96

#define E0_KPENTER (E0_BASE+0)
#define E0_RCTRL   (E0_BASE+1)
#define E0_KPSLASH (E0_BASE+2)
#define E0_PRSCR   (E0_BASE+3)
#define E0_RALT    (E0_BASE+4)
#define E0_BREAK   (E0_BASE+5)  /* (control-pause) */
#define E0_HOME    (E0_BASE+6)
#define E0_UP      (E0_BASE+7)
#define E0_PGUP    (E0_BASE+8)
#define E0_LEFT    (E0_BASE+9)
#define E0_RIGHT   (E0_BASE+10)
#define E0_END     (E0_BASE+11)
#define E0_DOWN    (E0_BASE+12)
#define E0_PGDN    (E0_BASE+13)
#define E0_INS     (E0_BASE+14)
#define E0_DEL     (E0_BASE+15)
/* BTC */
#define E0_MACRO   (E0_BASE+16)
/* LK450 */
#define E0_F13     (E0_BASE+17)
#define E0_F14     (E0_BASE+18)
#define E0_HELP    (E0_BASE+19)
#define E0_DO      (E0_BASE+20)
#define E0_F17     (E0_BASE+21)
#define E0_KPMINPLUS (E0_BASE+22)

#define E1_PAUSE   (E0_BASE+23)	                      /* 119 */

static unsigned char e0_keys[128] = {
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x00-0x07 */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x08-0x0f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x10-0x17 */
  0, 0, 0, 0, E0_KPENTER, E0_RCTRL, 0, 0,	      /* 0x18-0x1f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x20-0x27 */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x28-0x2f */
  0, 0, 0, 0, 0, E0_KPSLASH, 0, E0_PRSCR,	      /* 0x30-0x37 */
  E0_RALT, 0, 0, 0, 0, E0_F13, E0_F14, E0_HELP,	      /* 0x38-0x3f */
  E0_DO, E0_F17, 0, 0, 0, 0, E0_BREAK, E0_HOME,	      /* 0x40-0x47 */
  E0_UP, E0_PGUP, 0, E0_LEFT, 0, E0_RIGHT, E0_KPMINPLUS, E0_END,/* 0x48-0x4f */
  E0_DOWN, E0_PGDN, E0_INS, E0_DEL, 0, 0, 0, 0,	      /* 0x50-0x57 */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x58-0x5f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x60-0x67 */
  0, 0, 0, 0, 0, 0, 0, E0_MACRO,		      /* 0x68-0x6f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x70-0x77 */
  0, 0, 0, 0, 0, 0, 0, 0			      /* 0x78-0x7f */
};

/* kludge to stay below 128 - next time someone comes with a strange
   keyboard, key codes will have to become 2 (or 4) bytes. */
/* Owners of a FOCUS 9000 can assign F1,F2-F8,F9-F12 to 85,89-95,120-123 */
/* Owners of a certain Japanese keyboard can use 89 and 124 */
/* Owners of a certain Brazilian keyboard can use 89 and 121 */
/* Note: MEDIUMRAW mode will change, and all keycodes above 89 will change;
   this is only a temporary solution */

#define SC_LIM 89

#define FOCUS_PF1 85           /* actual code! */
#define FOCUS_PF2 89
#define FOCUS_PF3 90
#define FOCUS_PF4 91
#define FOCUS_PF5 92
#define FOCUS_PF6 93
#define FOCUS_PF7 94
#define FOCUS_PF8 95
#define FOCUS_PF9 (E1_PAUSE + 1)
#define FOCUS_PF10 (E1_PAUSE + 2)
#define FOCUS_PF11 (E1_PAUSE + 3)
#define FOCUS_PF12 (E1_PAUSE + 4)                    /* 123 */
#define JAP_86     (E1_PAUSE + 5)                    /* 124 */

static unsigned char high_keys[128 - SC_LIM] = {
  0, 0, 0, 0, 0, 0, 0,                               /* 0x59-0x5f */
  0, 0, 0, 0, 0, 0, 0, 0,                            /* 0x60-0x67 */
  0, 0, 0, 0, 0, FOCUS_PF11, 0, FOCUS_PF12,          /* 0x68-0x6f */
  0, 0, 0, FOCUS_PF2, FOCUS_PF9, 0, 0, FOCUS_PF3,    /* 0x70-0x77 */
  FOCUS_PF4, FOCUS_PF5, FOCUS_PF6, FOCUS_PF7,        /* 0x78-0x7b */
  FOCUS_PF8, JAP_86, FOCUS_PF10, 0                   /* 0x7c-0x7f */
};

static void keyboard_interrupt(int int_pt_regs)
{
	unsigned char scancode, keycode;
	static unsigned int prev_scancode = 0;   /* remember E0, E1 */
	char up_flag;				 /* 0 or 0200 */
	char raw_mode;

	pt_regs = (struct pt_regs *) int_pt_regs;
	send_cmd(0xAD);		/* disable keyboard */
	kb_wait();
	if ((inb_p(0x64) & kbd_read_mask) != 0x01)
		goto end_kbd_intr;
	scancode = inb(0x60);
	mark_bh(KEYBOARD_BH);
	if (reply_expected) {
	  /* 0xfa, 0xfe only mean "acknowledge", "resend" for most keyboards */
	  /* but they are the key-up scancodes for PF6, PF10 on a FOCUS 9000 */
		reply_expected = 0;
		if (scancode == 0xfa) {
			acknowledge = 1;
			goto end_kbd_intr;
		} else if (scancode == 0xfe) {
			resend = 1;
			goto end_kbd_intr;
		}
		/* strange ... */
		reply_expected = 1;
#ifdef KBD_REPORT_ERR
		printk("keyboard reply expected - got %02x\n", scancode);
#endif
	}
	if (scancode == 0) {
#ifdef KBD_REPORT_ERR
		printk("keyboard buffer overflow\n");
#endif
		prev_scancode = 0;
		goto end_kbd_intr;
	}
	if (scancode == 0xff) {
		/* the calculator keys on a FOCUS 9000 generate 0xff */
#ifndef KBD_IS_FOCUS_9000
#ifdef KBD_REPORT_ERR
		printk("keyboard error\n");
#endif
#endif
		prev_scancode = 0;
		goto end_kbd_intr;
	}

	tty = ttytab[fg_console];
 	kbd = kbd_table + fg_console;
	if ((raw_mode = (kbd->kbdmode == VC_RAW))) {
 		put_queue(scancode);
		/* we do not return yet, because we want to maintain
		   the key_down array, so that we have the correct
		   values when finishing RAW mode or when changing VT's */
 	}
	if (scancode == 0xe0 || scancode == 0xe1) {
		prev_scancode = scancode;
		goto end_kbd_intr;
 	}

 	/*
	 *  Convert scancode to keycode, using prev_scancode.
 	 */
	up_flag = (scancode & 0200);
 	scancode &= 0x7f;

	if (prev_scancode) {
	  /*
	   * usually it will be 0xe0, but a Pause key generates
	   * e1 1d 45 e1 9d c5 when pressed, and nothing when released
	   */
	  if (prev_scancode != 0xe0) {
	      if (prev_scancode == 0xe1 && scancode == 0x1d) {
		  prev_scancode = 0x100;
		  goto end_kbd_intr;
	      } else if (prev_scancode == 0x100 && scancode == 0x45) {
		  keycode = E1_PAUSE;
		  prev_scancode = 0;
	      } else {
#ifdef KBD_REPORT_UNKN
		  printk("keyboard: unknown e1 escape sequence\n");
#endif
		  prev_scancode = 0;
		  goto end_kbd_intr;
	      }
	  } else {
	      prev_scancode = 0;
	      /*
	       *  The keyboard maintains its own internal caps lock and
	       *  num lock statuses. In caps lock mode E0 AA precedes make
	       *  code and E0 2A follows break code. In num lock mode,
	       *  E0 2A precedes make code and E0 AA follows break code.
	       *  We do our own book-keeping, so we will just ignore these.
	       */
	      /*
	       *  For my keyboard there is no caps lock mode, but there are
	       *  both Shift-L and Shift-R modes. The former mode generates
	       *  E0 2A / E0 AA pairs, the latter E0 B6 / E0 36 pairs.
	       *  So, we should also ignore the latter. - aeb@cwi.nl
	       */
	      if (scancode == 0x2a || scancode == 0x36)
		goto end_kbd_intr;

	      if (e0_keys[scancode])
		keycode = e0_keys[scancode];
	      else {
#ifdef KBD_REPORT_UNKN
		  if (!raw_mode)
		    printk("keyboard: unknown scancode e0 %02x\n", scancode);
#endif
		  goto end_kbd_intr;
	      }
	  }
	} else if (scancode >= SC_LIM) {
	    /* This happens with the FOCUS 9000 keyboard
	       Its keys PF1..PF12 are reported to generate
	       55 73 77 78 79 7a 7b 7c 74 7e 6d 6f
	       Moreover, unless repeated, they do not generate
	       key-down events, so we have to zero up_flag below */
	    /* Also, Japanese 86/106 keyboards are reported to
	       generate 0x73 and 0x7d for \ - and \ | respectively. */
	    /* Also, some Brazilian keyboard is reported to produce
	       0x73 and 0x7e for \ ? and KP-dot, respectively. */

	  keycode = high_keys[scancode - SC_LIM];

	  if (!keycode) {
	      if (!raw_mode) {
#ifdef KBD_REPORT_UNKN
		  printk("keyboard: unrecognized scancode (%02x) - ignored\n"
			 , scancode);
#endif
	      }
	      goto end_kbd_intr;
	  }
 	} else
	  keycode = scancode;

	/*
	 * At this point the variable `keycode' contains the keycode.
	 * Note: the keycode must not be 0.
	 * We keep track of the up/down status of the key, and
	 * return the keycode if in MEDIUMRAW mode.
	 */

	if (up_flag) {
		rep = 0;
 		if(!clear_bit(keycode, key_down)) {
		    /* unexpected, but this can happen:
		       maybe this was a key release for a FOCUS 9000
		       PF key; if we want to see it, we have to clear
		       up_flag */
		    if (keycode >= SC_LIM || keycode == 85)
		      up_flag = 0;
		}
	} else
 		rep = set_bit(keycode, key_down);

	if (raw_mode)
		goto end_kbd_intr;

	if (kbd->kbdmode == VC_MEDIUMRAW) {
		/* soon keycodes will require more than one byte */
 		put_queue(keycode + up_flag);
		goto end_kbd_intr;
 	}

 	/*
	 * Small change in philosophy: earlier we defined repetition by
	 *	 rep = keycode == prev_keycode;
	 *	 prev_keycode = keycode;
	 * but now by the fact that the depressed key was down already.
	 * Does this ever make a difference? Yes.
	 */

	/*
 	 *  Repeat a key only if the input buffers are empty or the
 	 *  characters get echoed locally. This makes key repeat usable
 	 *  with slow applications and under heavy loads.
	 */
	if (!rep ||
	    (vc_kbd_mode(kbd,VC_REPEAT) && tty &&
	     (L_ECHO(tty) || (tty->driver.chars_in_buffer(tty) == 0)))) {
		u_short keysym;
		u_char type;

		/* the XOR below used to be an OR */
		int shift_final = shift_state ^ kbd->lockstate;
		ushort *key_map = key_maps[shift_final];

		if (key_map != NULL) {
			keysym = key_map[keycode];
			type = KTYP(keysym);

			if (type >= 0xf0) {
			    type -= 0xf0;
			    if (type == KT_LETTER) {
				type = KT_LATIN;
				if (vc_kbd_led(kbd, VC_CAPSLOCK)) {
				    key_map = key_maps[shift_final ^ (1<<KG_SHIFT)];
				    if (key_map)
				      keysym = key_map[keycode];
				}
			    }
			    (*key_handler[type])(keysym & 0xff, up_flag);
			} else {
			    /* maybe only if (kbd->kbdmode == VC_UNICODE) ? */
			    if (!up_flag)
			      to_utf8(keysym);
			}
		} else {
			/* maybe beep? */
			/* we have at least to update shift_state */
#if 0			/* how? two almost equivalent choices follow */
			compute_shiftstate();
#else
			keysym = U(plain_map[keycode]);
			type = KTYP(keysym);
			if (type == KT_SHIFT)
			  (*key_handler[type])(keysym & 0xff, up_flag);
#endif
		}
	}

end_kbd_intr:
	send_cmd(0xAE);         /* enable keyboard */
}

static void put_queue(int ch)
{
	wake_up(&keypress_wait);
	if (tty) {
		tty_insert_flip_char(tty, ch, 0);
		tty_schedule_flip(tty);
	}
}

static void puts_queue(char *cp)
{
	wake_up(&keypress_wait);
	if (!tty)
		return;

	while (*cp) {
		tty_insert_flip_char(tty, *cp, 0);
		cp++;
	}
	tty_schedule_flip(tty);
}

static void applkey(int key, char mode)
{
	static char buf[] = { 0x1b, 'O', 0x00, 0x00 };

	buf[1] = (mode ? 'O' : '[');
	buf[2] = key;
	puts_queue(buf);
}

static void enter(void)
{
	put_queue(13);
	if (vc_kbd_mode(kbd,VC_CRLF))
		put_queue(10);
}

static void caps_toggle(void)
{
	if (rep)
		return;
	chg_vc_kbd_led(kbd, VC_CAPSLOCK);
}

static void caps_on(void)
{
	if (rep)
		return;
	set_vc_kbd_led(kbd, VC_CAPSLOCK);
}

static void show_ptregs(void)
{
	if (!pt_regs)
		return;
	printk("\n");
	printk("EIP: %04x:%08lx",0xffff & pt_regs->cs,pt_regs->eip);
	if (pt_regs->cs & 3)
		printk(" ESP: %04x:%08lx",0xffff & pt_regs->ss,pt_regs->esp);
	printk(" EFLAGS: %08lx\n",pt_regs->eflags);
	printk("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
		pt_regs->orig_eax,pt_regs->ebx,pt_regs->ecx,pt_regs->edx);
	printk("ESI: %08lx EDI: %08lx EBP: %08lx",
		pt_regs->esi, pt_regs->edi, pt_regs->ebp);
	printk(" DS: %04x ES: %04x FS: %04x GS: %04x\n",
		0xffff & pt_regs->ds,0xffff & pt_regs->es,
		0xffff & pt_regs->fs,0xffff & pt_regs->gs);
}

static void hold(void)
{
	if (rep || !tty)
		return;

	/*
	 * Note: SCROLLOCK will be set (cleared) by stop_tty (start_tty);
	 * these routines are also activated by ^S/^Q.
	 * (And SCROLLOCK can also be set by the ioctl KDSKBLED.)
	 */
	if (tty->stopped)
		start_tty(tty);
	else
		stop_tty(tty);
}

static void num(void)
{
	if (vc_kbd_mode(kbd,VC_APPLIC)) {
		applkey('P', 1);
		return;
	}
	if (!rep)	/* no autorepeat for numlock, ChN */
		chg_vc_kbd_led(kbd,VC_NUMLOCK);
}

static void lastcons(void)
{
	/* switch to the last used console, ChN */
	want_console = last_console;
}

static void decr_console(void)
{
	int i;
 
	for (i = fg_console-1; i != fg_console; i--) {
		if (i == -1)
			i = MAX_NR_CONSOLES-1;
		if (vc_cons_allocated(i))
			break;
	}
	want_console = i;
}

static void incr_console(void)
{
	int i;

	for (i = fg_console+1; i != fg_console; i++) {
		if (i == MAX_NR_CONSOLES)
			i = 0;
		if (vc_cons_allocated(i))
			break;
	}
	want_console = i;
}

static void send_intr(void)
{
	if (!tty || (tty->termios && I_IGNBRK(tty)))
		return;
	tty_insert_flip_char(tty, 0, TTY_BREAK);
}

static void scroll_forw(void)
{
	scrollfront(0);
}

static void scroll_back(void)
{
	scrollback(0);
}

static void boot_it(void)
{
	ctrl_alt_del();
}

static void compose(void)
{
	dead_key_next = 1;
}

static void SAK(void)
{
	do_SAK(tty);
#if 0
	/*
	 * Need to fix SAK handling to fix up RAW/MEDIUM_RAW and
	 * vt_cons modes before we can enable RAW/MEDIUM_RAW SAK
	 * handling.
	 * 
	 * We should do this some day --- the whole point of a secure
	 * attention key is that it should be guaranteed to always
	 * work.
	 */
	reset_vc(fg_console);
	unblank_screen();	/* not in interrupt routine? */
#endif
}

static void do_ignore(unsigned char value, char up_flag)
{
}

static void do_spec(unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	if (value >= SIZE(spec_fn_table))
		return;
	if (!spec_fn_table[value])
		return;
	spec_fn_table[value]();
}

static void do_lowercase(unsigned char value, char up_flag)
{
	printk("keyboard.c: do_lowercase was called - impossible\n");
}

static void do_self(unsigned char value, char up_flag)
{
	if (up_flag)
		return;		/* no action, if this is a key release */

	if (diacr)
		value = handle_diacr(value);

	if (dead_key_next) {
		dead_key_next = 0;
		diacr = value;
		return;
	}

	put_queue(value);
}

#define A_GRAVE  '`'
#define A_ACUTE  '\''
#define A_CFLEX  '^'
#define A_TILDE  '~'
#define A_DIAER  '"'
static unsigned char ret_diacr[] =
	{A_GRAVE, A_ACUTE, A_CFLEX, A_TILDE, A_DIAER };

/* If a dead key pressed twice, output a character corresponding to it,	*/
/* otherwise just remember the dead key.				*/

static void do_dead(unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	value = ret_diacr[value];
	if (diacr == value) {   /* pressed twice */
		diacr = 0;
		put_queue(value);
		return;
	}
	diacr = value;
}


/* If space is pressed, return the character corresponding the pending	*/
/* dead key, otherwise try to combine the two.				*/

unsigned char handle_diacr(unsigned char ch)
{
	int d = diacr;
	int i;

	diacr = 0;
	if (ch == ' ')
		return d;

	for (i = 0; i < accent_table_size; i++) {
		if (accent_table[i].diacr == d && accent_table[i].base == ch)
			return accent_table[i].result;
	}

	put_queue(d);
	return ch;
}

static void do_cons(unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	want_console = value;
}

static void do_fn(unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	if (value < SIZE(func_table)) {
		if (func_table[value])
			puts_queue(func_table[value]);
	} else
		printk("do_fn called with value=%d\n", value);
}

static void do_pad(unsigned char value, char up_flag)
{
	static char *pad_chars = "0123456789+-*/\015,.?";
	static char *app_map = "pqrstuvwxylSRQMnn?";

	if (up_flag)
		return;		/* no action, if this is a key release */

	/* kludge... shift forces cursor/number keys */
	if (vc_kbd_mode(kbd,VC_APPLIC) && !k_down[KG_SHIFT]) {
		applkey(app_map[value], 1);
		return;
	}

	if (!vc_kbd_led(kbd,VC_NUMLOCK))
		switch (value) {
			case KVAL(K_PCOMMA):
			case KVAL(K_PDOT):
				do_fn(KVAL(K_REMOVE), 0);
				return;
			case KVAL(K_P0):
				do_fn(KVAL(K_INSERT), 0);
				return;
			case KVAL(K_P1):
				do_fn(KVAL(K_SELECT), 0);
				return;
			case KVAL(K_P2):
				do_cur(KVAL(K_DOWN), 0);
				return;
			case KVAL(K_P3):
				do_fn(KVAL(K_PGDN), 0);
				return;
			case KVAL(K_P4):
				do_cur(KVAL(K_LEFT), 0);
				return;
			case KVAL(K_P6):
				do_cur(KVAL(K_RIGHT), 0);
				return;
			case KVAL(K_P7):
				do_fn(KVAL(K_FIND), 0);
				return;
			case KVAL(K_P8):
				do_cur(KVAL(K_UP), 0);
				return;
			case KVAL(K_P9):
				do_fn(KVAL(K_PGUP), 0);
				return;
			case KVAL(K_P5):
				applkey('G', vc_kbd_mode(kbd, VC_APPLIC));
				return;
		}

	put_queue(pad_chars[value]);
	if (value == KVAL(K_PENTER) && vc_kbd_mode(kbd, VC_CRLF))
		put_queue(10);
}

static void do_cur(unsigned char value, char up_flag)
{
	static char *cur_chars = "BDCA";
	if (up_flag)
		return;

	applkey(cur_chars[value], vc_kbd_mode(kbd,VC_CKMODE));
}

static void do_shift(unsigned char value, char up_flag)
{
	int old_state = shift_state;

	if (rep)
		return;

	/* Mimic typewriter:
	   a CapsShift key acts like Shift but undoes CapsLock */
	if (value == KVAL(K_CAPSSHIFT)) {
		value = KVAL(K_SHIFT);
		if (!up_flag)
			clr_vc_kbd_led(kbd, VC_CAPSLOCK);
	}

	if (up_flag) {
		/* handle the case that two shift or control
		   keys are depressed simultaneously */
		if (k_down[value])
			k_down[value]--;
	} else
		k_down[value]++;

	if (k_down[value])
		shift_state |= (1 << value);
	else
		shift_state &= ~ (1 << value);

	/* kludge */
	if (up_flag && shift_state != old_state && npadch != -1) {
		if (kbd->kbdmode == VC_UNICODE)
		  to_utf8(npadch & 0xffff);
		else
		  put_queue(npadch & 0xff);
		npadch = -1;
	}
}

/* called after returning from RAW mode or when changing consoles -
   recompute k_down[] and shift_state from key_down[] */
/* maybe called when keymap is undefined, so that shiftkey release is seen */
void compute_shiftstate(void)
{
	int i, j, k, sym, val;

	shift_state = 0;
	for(i=0; i < SIZE(k_down); i++)
	  k_down[i] = 0;

	for(i=0; i < SIZE(key_down); i++)
	  if(key_down[i]) {	/* skip this word if not a single bit on */
	    k = (i<<5);
	    for(j=0; j<32; j++,k++)
	      if(test_bit(k, key_down)) {
		sym = U(plain_map[k]);
		if(KTYP(sym) == KT_SHIFT) {
		  val = KVAL(sym);
		  if (val == KVAL(K_CAPSSHIFT))
		    val = KVAL(K_SHIFT);
		  k_down[val]++;
		  shift_state |= (1<<val);
		}
	      }
	  }
}

static void do_meta(unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	if (vc_kbd_mode(kbd, VC_META)) {
		put_queue('\033');
		put_queue(value);
	} else
		put_queue(value | 0x80);
}

static void do_ascii(unsigned char value, char up_flag)
{
	int base;

	if (up_flag)
		return;

	if (value < 10)    /* decimal input of code, while Alt depressed */
	    base = 10;
	else {       /* hexadecimal input of code, while AltGr depressed */
	    value -= 10;
	    base = 16;
	}

	if (npadch == -1)
	  npadch = value;
	else
	  npadch = npadch * base + value;
}

static void do_lock(unsigned char value, char up_flag)
{
	if (up_flag || rep)
		return;
	chg_vc_kbd_lock(kbd, value);
}

/*
 * send_data sends a character to the keyboard and waits
 * for a acknowledge, possibly retrying if asked to. Returns
 * the success status.
 */
static int send_data(unsigned char data)
{
	int retries = 3;
	int i;

	do {
		kb_wait();
		acknowledge = 0;
		resend = 0;
		reply_expected = 1;
		outb_p(data, 0x60);
		for(i=0; i<0x20000; i++) {
			inb_p(0x64);		/* just as a delay */
			if (acknowledge)
				return 1;
			if (resend)
				break;
		}
		if (!resend)
			return 0;
	} while (retries-- > 0);
	return 0;
}

/*
 * The leds display either (i) the status of NumLock, CapsLock, ScrollLock,
 * or (ii) whatever pattern of lights people want to show using KDSETLED,
 * or (iii) specified bits of specified words in kernel memory.
 */

static unsigned char ledstate = 0xff; /* undefined */
static unsigned char ledioctl;

unsigned char getledstate(void) {
    return ledstate;
}

void setledstate(struct kbd_struct *kbd, unsigned int led) {
    if (!(led & ~7)) {
	ledioctl = led;
	kbd->ledmode = LED_SHOW_IOCTL;
    } else
	kbd->ledmode = LED_SHOW_FLAGS;
    set_leds();
}

static struct ledptr {
    unsigned int *addr;
    unsigned int mask;
    unsigned char valid:1;
} ledptrs[3];

void register_leds(int console, unsigned int led,
		   unsigned int *addr, unsigned int mask) {
    struct kbd_struct *kbd = kbd_table + console;
    if (led < 3) {
	ledptrs[led].addr = addr;
	ledptrs[led].mask = mask;
	ledptrs[led].valid = 1;
	kbd->ledmode = LED_SHOW_MEM;
    } else
	kbd->ledmode = LED_SHOW_FLAGS;
}

static inline unsigned char getleds(void){
    struct kbd_struct *kbd = kbd_table + fg_console;
    unsigned char leds;

    if (kbd->ledmode == LED_SHOW_IOCTL)
      return ledioctl;
    leds = kbd->ledflagstate;
    if (kbd->ledmode == LED_SHOW_MEM) {
	if (ledptrs[0].valid) {
	    if (*ledptrs[0].addr & ledptrs[0].mask)
	      leds |= 1;
	    else
	      leds &= ~1;
	}
	if (ledptrs[1].valid) {
	    if (*ledptrs[1].addr & ledptrs[1].mask)
	      leds |= 2;
	    else
	      leds &= ~2;
	}
	if (ledptrs[2].valid) {
	    if (*ledptrs[2].addr & ledptrs[2].mask)
	      leds |= 4;
	    else
	      leds &= ~4;
	}
    }
    return leds;
}

/*
 * This routine is the bottom half of the keyboard interrupt
 * routine, and runs with all interrupts enabled. It does
 * console changing, led setting and copy_to_cooked, which can
 * take a reasonably long time.
 *
 * Aside from timing (which isn't really that important for
 * keyboard interrupts as they happen often), using the software
 * interrupt routines for this thing allows us to easily mask
 * this when we don't want any of the above to happen. Not yet
 * used, but this allows for easy and efficient race-condition
 * prevention later on.
 */
static void kbd_bh(void * unused)
{
	unsigned char leds = getleds();

	if (leds != ledstate) {
		ledstate = leds;
		if (!send_data(0xed) || !send_data(leds))
			send_data(0xf4);	/* re-enable kbd if any errors */
	}
	if (want_console >= 0) {
		if (want_console != fg_console) {
			last_console = fg_console;
			change_console(want_console);
			/* we only changed when the console had already
			   been allocated - a new console is not created
			   in an interrupt routine */
		}
		want_console = -1;
	}
	poke_blanked_console();
	cli();
	if ((inb_p(0x64) & kbd_read_mask) == 0x01)
		fake_keyboard_interrupt();
	sti();
}

long no_idt[2] = {0, 0};

/*
 * This routine reboots the machine by asking the keyboard
 * controller to pulse the reset-line low. We try that for a while,
 * and if it doesn't work, we do some other stupid things.
 */
void hard_reset_now(void)
{
	int i, j;
	extern unsigned long pg0[1024];

	sti();
/* rebooting needs to touch the page at absolute addr 0 */
	pg0[0] = 7;
	*((unsigned short *)0x472) = 0x1234;
	for (;;) {
		for (i=0; i<100; i++) {
			kb_wait();
			for(j = 0; j < 100000 ; j++)
				/* nothing */;
			outb(0xfe,0x64);	 /* pulse reset low */
		}
		__asm__("\tlidt _no_idt");
	}
}

unsigned long kbd_init(unsigned long kmem_start)
{
	int i;
	struct kbd_struct kbd0;
	extern struct tty_driver console_driver;

	kbd0.ledflagstate = kbd0.default_ledflagstate = KBD_DEFLEDS;
	kbd0.ledmode = LED_SHOW_FLAGS;
	kbd0.lockstate = KBD_DEFLOCK;
	kbd0.modeflags = KBD_DEFMODE;
	kbd0.kbdmode = VC_XLATE;
 
	for (i = 0 ; i < MAX_NR_CONSOLES ; i++)
		kbd_table[i] = kbd0;

	ttytab = console_driver.table;

	bh_base[KEYBOARD_BH].routine = kbd_bh;
	request_irq(KEYBOARD_IRQ, keyboard_interrupt, 0, "keyboard");
	mark_bh(KEYBOARD_BH);
	return kmem_start;
}
