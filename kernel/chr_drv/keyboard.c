/*
 * linux/kernel/chr_drv/keyboard.c
 *
 * Keyboard driver for Linux v0.96 using Latin-1.
 *
 * Written for linux by Johan Myreen as a translation from
 * the assembly version by Linus (with diacriticals added)
 *
 * Some additional features added by Christoph Niemann (ChN), March 1993
 *
 * Loadable keymaps by Risto Kankkunen, May 1993
 */

#define KEYBOARD_IRQ 1

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/keyboard.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/signal.h>

#ifndef KBD_DEFFLAGS
#ifdef CONFIG_KBD_META
#define KBD_DEFFLAGS ((1 << VC_NUMLOCK) | (1 << VC_REPEAT) | (1 << VC_META))
#else
#define KBD_DEFFLAGS ((1 << VC_NUMLOCK) | (1 << VC_REPEAT))
#endif
#endif

#define SHIFT_KEYS	((1 << KG_LSHIFT) | (1 << KG_RSHIFT))
#define CTRL_KEYS	((1 << KG_LCTRL)  | (1 << KG_RCTRL))
#define ALT_KEYS	((1 << KG_LALT)   | (1 << KG_RALT))
#define ALTGR_KEYS	((1 << KG_LALTGR) | (1 << KG_RALTGR))

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

extern void do_keyboard_interrupt(void);
extern void ctrl_alt_del(void);
extern void change_console(unsigned int new_console);
extern void scrollback(int);
extern void scrollfront(int);

#define fake_keyboard_interrupt() \
__asm__ __volatile__("int $0x21")

unsigned long kbd_flags = 0;
unsigned long kbd_dead_keys = 0;
unsigned long kbd_prev_dead_keys = 0;

static int want_console = -1;
static int last_console = 0;		/* last used VC */
static char rep = 0;			/* flag telling character repeat */
struct kbd_struct kbd_table[NR_CONSOLES];
static struct kbd_struct * kbd = kbd_table;
static struct tty_struct * tty = NULL;

static volatile unsigned char acknowledge = 0;
static volatile unsigned char resend = 0;

typedef unsigned short u_word;
typedef void (*k_hand)(unsigned char value, char up_flag);

static void do_self(unsigned char value, char up_flag);
static void do_fn(unsigned char value, char up_flag);
static void do_spec(unsigned char value, char up_flag);
static void do_pad(unsigned char value, char up_flag);
static void do_dead(unsigned char value, char up_flag);
static void do_cons(unsigned char value, char up_flag);
static void do_cur(unsigned char value, char up_flag);
static void do_shift(unsigned char value, char up_flag);

static k_hand key_handler[] = {
	do_self, do_fn, do_spec, do_pad, do_dead, do_cons, do_cur, do_shift
};

/* maximum values each key_handler can handle */
const u_char max_vals[] = {
	255, 25, 13, 16, 4, 255, 3, 255
};

const int NR_TYPES = (sizeof(max_vals) / sizeof(u_char));

#define E0_BASE 96

static int shift_state = 0;
static int diacr = -1;
static int npadch = 0;

static void put_queue(int);
static unsigned int handle_diacr(unsigned int);

static struct pt_regs * pt_regs;

static inline void kb_wait(void)
{
	int i;

	for (i=0; i<0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

static void keyboard_interrupt(int int_pt_regs)
{
	unsigned char scancode;
	static unsigned char prev_scancode = 0;

	pt_regs = (struct pt_regs *) int_pt_regs;
	kbd_prev_dead_keys |= kbd_dead_keys;
	if (!kbd_dead_keys)
		kbd_prev_dead_keys = 0;
	kbd_dead_keys = 0;
	kb_wait();
	if (!(inb_p(0x64) & 0x01))
		goto end_kbd_intr;
	scancode = inb(0x60);
	mark_bh(KEYBOARD_BH);
	if (scancode == 0xfa) {
		acknowledge = 1;
		goto end_kbd_intr;
	} else if (scancode == 0xfe) {
		resend = 1;
		goto end_kbd_intr;
	}
	tty = TTY_TABLE(0);
	kbd = kbd_table + fg_console;
	if (vc_kbd_flag(kbd,VC_RAW)) {
		kbd_flags = 0;
		put_queue(scancode);
		goto end_kbd_intr;
	}
	if (scancode == 0xe0) {
		set_kbd_dead(KGD_E0);
		goto end_kbd_intr;
	} else if (scancode == 0xe1) {
		set_kbd_dead(KGD_E1);
		goto end_kbd_intr;
	}
	/*
	 *  The keyboard maintains its own internal caps lock and num lock
	 *  statuses. In caps lock mode E0 AA precedes make code and E0 2A
	 *  follows break code. In num lock mode, E0 2A precedes make
	 *  code and E0 AA follows break code. We do our own book-keeping,
	 *  so we will just ignore these.
	 */
	if (kbd_dead(KGD_E0) && (scancode == 0x2a || scancode == 0xaa))
		goto end_kbd_intr;
	/*
	 *  Repeat a key only if the input buffers are empty or the
	 *  characters get echoed locally. This makes key repeat usable
	 *  with slow applications and under heavy loads.
	 */
	rep = scancode == prev_scancode;
	prev_scancode = scancode;
	if (!rep || 
	    (vc_kbd_flag(kbd,VC_REPEAT) && tty &&
	     (L_ECHO(tty) || (EMPTY(&tty->secondary) && EMPTY(&tty->read_q))))) {
	     	static unsigned char e0_keys[] = {
			0x1c,	/* keypad enter */
			0x1d,	/* right control */
			0x35,	/* keypad slash */
			0x37,	/* print screen */
			0x38,	/* right alt */
			0x46,	/* break (control-pause) */
			0x47,	/* editpad home */
			0x48,	/* editpad up */
			0x49,	/* editpad pgup */
			0x4b,	/* editpad left */
			0x4d,	/* editpad right */
			0x4f,	/* editpad end */
			0x50,	/* editpad dn */
			0x51,	/* editpad pgdn */
			0x52,	/* editpad ins */
			0x53	/* editpad del */
		};
		u_word key_code;
		char break_flag = scancode > 0x7f;

		scancode &= 0x7f;
		if (scancode >= E0_BASE) {
#if 0
			printk("keyboard: scancode (%02x) not in range 00 - %2x\n", scancode, E0_BASE - 1);
#endif
			scancode = 0;
		}

		if (kbd_dead(KGD_E0)) {
			int i;
			for (i = 0; i < sizeof(e0_keys); i++)
				if (scancode == e0_keys[i]) {
					scancode = E0_BASE + i;
					i = -1;
					break;
				}
			if (i != -1)
				printk("keyboard: unknown scancode e0 %02x\n", scancode);
		}

		key_code = key_map[shift_state][scancode];
		(*key_handler[key_code >> 8])(key_code & 0xff, break_flag);
	     }
end_kbd_intr:
}

static void put_queue(int ch)
{
	struct tty_queue *qp;

	wake_up(&keypress_wait);
	if (!tty)
		return;
	qp = &tty->read_q;

	if (LEFT(qp)) {
		qp->buf[qp->head] = ch;
		INC(qp->head);
		wake_up_interruptible(&qp->proc_list);
	}
}

static void puts_queue(char *cp)
{
	struct tty_queue *qp;
	char ch;

	/* why interruptible here, plain wake_up above? */
	wake_up_interruptible(&keypress_wait);
	if (!tty)
		return;
	qp = &tty->read_q;

	while ((ch = *(cp++)) != 0) {
		if (LEFT(qp)) {
			qp->buf[qp->head] = ch;
			INC(qp->head);
		}
	}
	wake_up_interruptible(&qp->proc_list);
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
	if (vc_kbd_flag(kbd,VC_CRLF))
		put_queue(10);
}

static void caps_toggle(void)
{
	if (rep)
		return;
	set_kbd_flag(KG_CAPSLOCK);
	chg_vc_kbd_flag(kbd,VC_CAPSLOCK);
}

static void show_ptregs(void)
{
	if (!pt_regs)
		return;
	printk("\n");
	printk("EIP: %04x:%08x",0xffff & pt_regs->cs,pt_regs->eip);
	if (pt_regs->cs & 3)
		printk(" ESP: %04x:%08x",0xffff & pt_regs->cs,pt_regs->eip);
	printk(" EFLAGS: %08x\n",pt_regs->eflags);
	printk("EAX: %08x EBX: %08x ECX: %08x EDX: %08x\n",
		pt_regs->orig_eax,pt_regs->ebx,pt_regs->ecx,pt_regs->edx);
	printk("ESI: %08x EDI: %08x EBP: %08x",
		pt_regs->esi, pt_regs->edi, pt_regs->ebp);
	printk(" DS: %04x ES: %04x FS: %04x GS: %04x\n",
		0xffff & pt_regs->ds,0xffff & pt_regs->es,
		0xffff & pt_regs->fs,0xffff & pt_regs->gs);
}

static void hold(void)
{
	/* kludge, should have a control key table */
	if (kbd_flags & CTRL_KEYS) {
		show_state();
		return;
	}

	if (rep || !tty)
		return;
	if (vc_kbd_flag(kbd, VC_SCROLLOCK))
		/* pressing srcoll lock 2nd time sends ^Q, ChN */
		put_queue(START_CHAR(tty));
	else
		/* pressing srcoll lock 1st time sends ^S, ChN */
		put_queue(STOP_CHAR(tty));
	chg_vc_kbd_flag(kbd,VC_SCROLLOCK);
}

static void num(void)
{
#if 0
	if (kbd_flags & CTRL_KEYS) {
		/* pause key pressed, sends E1 1D 45, ChN */
		chg_vc_kbd_flag(kbd,VC_PAUSE);
		return;
	}
#endif
	if (vc_kbd_flag(kbd,VC_APPLIC)) {
		applkey('P', 1);
		return;
	}
	if (!rep)	/* no autorepeat for numlock, ChN */
		chg_vc_kbd_flag(kbd,VC_NUMLOCK);
}

static void lastcons(void)
{
	/* pressing alt-printscreen switches to the last used console, ChN */
	want_console = last_console;
}

static void send_intr(void)
{
	if (tty)
		put_queue(INTR_CHAR(tty));
}

static void do_spec(unsigned char value, char up_flag)
{
	typedef void (*fnp)(void);
	fnp fn_table[] = {
		NULL,		enter,		show_ptregs,	show_mem,
		show_state,	send_intr,	lastcons, 	caps_toggle,
		num,		hold
	};

	if (value >= sizeof(fn_table)/sizeof(fnp))
		return;
	if (up_flag)
		return;
	if (!fn_table[value])
		return;
	fn_table[value]();
}
  
static void do_self(unsigned char value, char up_flag)
{
	if (up_flag)
		return;		/* no action, if this is a key release */

	value = handle_diacr(value);

	/* kludge... */
	if (vc_kbd_flag(kbd,VC_CAPSLOCK))
		if ((value >= 'a' && value <= 'z')
		    || (value >= 224 && value <= 254)) {
			value -= 32;
		}
	/* kludge... */
	if (kbd_flags & CTRL_KEYS) {
		if (value >= 64 && value < 127)
			value &= 0x1f;
		else if (value == ' ' || value == '2')
			value = 0;
		else if (value >= '3' && value < '8')
			value -= 24;
		else if (value == '?' || value == '8' || value == '/')
			value = 127;
		else if (value == '-' || value == '_')
			value = 0x1f;
		else
			return;
	}

	if (kbd_flags & ALT_KEYS)
		if (vc_kbd_flag(kbd,VC_META)) {
			put_queue('\033');
			put_queue(value);
		} else
			put_queue(value|0x80);
	else
		put_queue(value);
}

static unsigned char ret_diacr[] =
	{'`', '\'', '^', '~', '"' };		/* Must not end with 0 */

/* If a dead key pressed twice, output a character corresponding it,	*/
/* otherwise just remember the dead key.				*/

static void do_dead(unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	if (diacr == value) {	/* pressed twice */
		diacr = -1;
		put_queue(ret_diacr[value]);
		return;
	}
	diacr = value;
}

/* If no pending dead key, return the character unchanged. Otherwise,	*/
/* if space if pressed, return a character corresponding the pending	*/
/* dead key, otherwise try to combine the two.				*/

unsigned int handle_diacr(unsigned int ch)
{
	static unsigned char accent_table[5][64] = {
	" \300BCD\310FGH\314JKLMN\322PQRST\331VWXYZ[\\]^_"
	"`\340bcd\350fgh\354jklmn\362pqrst\371vwxyz{|}~",   /* accent grave */

	" \301BCD\311FGH\315JKLMN\323PQRST\332VWX\335Z[\\]^_"
	"`\341bcd\351fgh\355jklmn\363pqrst\372vwxyz{|}~",   /* accent acute */

	" \302BCD\312FGH\316JKLMN\324PQRST\333VWXYZ[\\]^_"
	"`\342bcd\352fgh\356jklmn\364pqrst\373vwxyz{|}~",   /* circumflex */

	" \303BCDEFGHIJKLMN\325PQRSTUVWXYZ[\\]^_"
	"`\343bcdefghijklm\361\365pqrstuvwxyz{|}~",	    /* tilde */

	" \304BCD\313FGH\316JKLMN\326PQRST\334VWXYZ[\\]^_"
	"`\344bcd\353fgh\357jklmn\366pqrst\374vwx\377z{|}~" /* dieresis */
	};
	int d = diacr;

	if (diacr == -1)
		return ch;

	diacr = -1;
	if (ch == ' ')
		return ret_diacr[d];

	if (ch < 64 || ch > 122)
		return ch;

	return accent_table[d][ch - 64];
}

static void do_cons(unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	want_console = value;
}

static char *func_table[] = {
	"\033[[A",  "\033[[B",  "\033[[C",  "\033[[D",  "\033[[E", /* F1 -F5  */
	"\033[17~", "\033[18~", "\033[19~", "\033[20~", "\033[21~",/* F6 -F10 */
	"\033[23~", "\033[24~", "\033[25~", "\033[26~", "\033[28~",/* F11-F15 */
	"\033[29~", "\033[31~", "\033[32~", "\033[33~", "\033[34~",/* F16-F20 */
	"\033[1~",	/* Find */
	"\033[2~",	/* Insert */
	"\033[3~",	/* Remove */
	"\033[4~",	/* Select */
	"\033[5~",	/* Prev page */
	"\033[6~"	/* Next page */
};

static void do_fn(unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	if (kbd_flags & SHIFT_KEYS) {
		if (value == KVAL(K_PGDN)) {
			scrollfront(0);
			return;
		}
		if (value == KVAL(K_PGUP)) {
			scrollback(0);
			return;
		}
	}
	if (kbd_flags & ALT_KEYS) {
		want_console = value;
		return;
	}
	puts_queue(func_table[value]);
}

static void do_pad(unsigned char value, char up_flag)
{
	static char *pad_chars = "0123456789+-*/\015,.";
	static char *app_map = "pqrstuvwxylSRQMnn";

	if (up_flag)
		return;		/* no action, if this is a key release */

	if ((value == KVAL(K_PCOMMA) || value == KVAL(K_PDOT)) &&
	    (kbd_flags & CTRL_KEYS) && (kbd_flags & (ALT_KEYS | ALTGR_KEYS))) {
		ctrl_alt_del();
		return;
	}

	if ((kbd_flags & ALT_KEYS) && value <= 9) {	/* Alt-numpad */
		npadch = (npadch * 10 + value) % 1000;
		return;
	}

	/* kludge... shift forces cursor/number keys */
	if (vc_kbd_flag(kbd,VC_APPLIC) && shift_state != 1) {
		applkey(app_map[value], 1);
		return;
	}

	if (!vc_kbd_flag(kbd,VC_NUMLOCK))
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
				applkey('G', vc_kbd_flag(kbd,VC_APPLIC));
				return;
		}

	put_queue(pad_chars[value]);
	if (value == KVAL(K_PENTER) && vc_kbd_flag(kbd,VC_CRLF))
		put_queue(10);
}

static void do_cur(unsigned char value, char up_flag)
{
	static char *cur_chars = "BDCA";
	if (up_flag)
		return;

	applkey(cur_chars[value], vc_kbd_flag(kbd,VC_CKMODE));
}

static void do_shift(unsigned char value, char up_flag)
{
	shift_state = 0;
	if (up_flag)
		clr_kbd_flag(value);
	else
		set_kbd_flag(value);
	if (kbd_flags & SHIFT_KEYS)
		shift_state = 1;
	if (kbd_flags & ALTGR_KEYS)
		shift_state = 2;
	/* cludge */
	if (up_flag && value == KG_LALT && npadch != 0) {
		put_queue(npadch);
		npadch=0;
	}
}

#define C(x)		((KT_CONS<<8)|x)

u_word key_map[NR_KEYMAPS][NR_KEYS] = {
	{ /* unshifted keys */
	K_HOLE,  27,      '1',     '2',     '3',     '4',     '5',     '6',
	'7',     '8',     '9',     '0',     '-',     '=',     127,       9,
	'q',     'w',     'e',     'r',     't',     'y',     'u',     'i',
	'o',     'p',     '[',     ']', K_ENTER, K_LCTRL,     'a',     's',
	'd',     'f',     'g',     'h',     'j',     'k',     'l',     ';',
	'\'',    '`',  K_LSHIFT,  '\\',     'z',     'x',     'c',     'v',
	'b',     'n',     'm',     ',',     '.',     '/',     K_RSHIFT, K_PSTAR,
	K_ALT,   ' ',     K_CAPS,  K_F1,    K_F2,    K_F3,    K_F4,    K_F5,
	K_F6,    K_F7,    K_F8,    K_F9,    K_F10,   K_NUM,   K_HOLD,  K_P7,
	K_P8,    K_P9,    K_PMINUS, K_P4,   K_P5,    K_P6,    K_PPLUS, K_P1,
	K_P2,    K_P3,    K_P0,    K_PDOT, K_CONS, K_HOLE,    '<',    K_F11,
	K_F12,   K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,
	/* unshifted e0 keys */
	K_PENTER, K_RCTRL, K_PSLASH,    28, K_ALTGR, K_BREAK,  K_FIND,    K_UP,
	K_PGUP,   K_LEFT, K_RIGHT, K_SELECT, K_DOWN,  K_PGDN, K_INSERT, K_REMOVE },
	{ /* shifted keys */
	 K_HOLE,      27,     '!',     '@',     '#',     '$',     '%',     '^',
	    '&',     '*',     '(',     ')',     '_',     '+',     127,       9,
	    'Q',     'W',     'E',     'R',     'T',     'Y',     'U',     'I',
	    'O',     'P',     '{',     '}', K_ENTER,  K_LCTRL,    'A',     'S',
	    'D',     'F',     'G',     'H',     'J',     'K',     'L',     ':',
	    '"',     '~', K_LSHIFT,    '|',     'Z',     'X',     'C',     'V',
	    'B',     'N',     'M',     '<',     '>',     '?', K_RSHIFT, K_PSTAR,
	  K_ALT,      32,  K_CAPS,   K_F11,   K_F12,   K_F13,   K_F14,   K_F15,
	  K_F16,   K_F17,   K_F18,   K_F19,   K_F20,   K_NUM, K_SH_MEM,   K_P7,
	   K_P8,    K_P9, K_PMINUS,   K_P4,    K_P5,    K_P6, K_PPLUS,    K_P1,
	   K_P2,    K_P3,    K_P0,  K_PDOT,  K_CONS,  K_HOLE,     '>',   K_F11,
	  K_F12,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,
	/* shifted e0 keys */
	K_PENTER, K_RCTRL, K_PSLASH, K_HOLE, K_ALTGR, K_BREAK, K_FIND,  K_UP,
	K_PGUP,   K_LEFT, K_RIGHT, K_SELECT, K_DOWN, K_PGDN, K_INSERT, K_REMOVE },
	{ /* alted keys */
	 K_HOLE,  K_HOLE,  K_HOLE,     '@',  K_HOLE,     '$',  K_HOLE,  K_HOLE,
	    '{',     '[',     ']',     '}',    '\\',  K_HOLE,  K_HOLE,  K_HOLE,
	 K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,
	 K_HOLE,  K_HOLE,  K_HOLE,     '~', K_ENTER, K_LCTRL,  K_HOLE,  K_HOLE,
	 K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,
	 K_HOLE,  K_HOLE, K_LSHIFT, K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,
	 K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE, K_RSHIFT, K_HOLE,
	  K_ALT,  K_HOLE,  K_CAPS,   C(12),   C(13),   C(14),   C(15),   C(16),
	  C(17),   C(18),   C(19),   C(20),   C(21),   K_NUM, K_SH_REGS,  K_P7,
	   K_P8,    K_P9, K_PMINUS,   K_P4,    K_P5,    K_P6, K_PPLUS,    K_P1,
	   K_P2,    K_P3,    K_P0,  K_PDOT,  K_CONS,  K_HOLE,     '|',   C(22),
	  C(23),  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,  K_HOLE,
	/* alted e0 keys */
	K_PENTER, K_RCTRL, K_PSLASH, K_HOLE, K_ALTGR, K_BREAK, K_FIND,    K_UP,
	K_PGUP,   K_LEFT, K_RIGHT, K_SELECT, K_DOWN, K_PGDN, K_INSERT, K_REMOVE }};

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
		outb_p(data, 0x60);
		for(i=0; i<0x20000; i++) {
			inb_p(0x64);		/* just as a delay */
			if (acknowledge)
				return 1;
			if (resend)
				goto repeat;
		}
		return 0;
repeat:
	} while (retries-- > 0);
	return 0;
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
	static unsigned char old_leds = -1;
	unsigned char leds = kbd_table[fg_console].flags & LED_MASK;

	if (leds != old_leds) {
		old_leds = leds;
		if (!send_data(0xed) || !send_data(leds))
			send_data(0xf4);	/* re-enable kbd if any errors */
	}
	if (want_console >= 0) {
		if (want_console != fg_console) {
			last_console = fg_console;
			change_console(want_console);
		}
		want_console = -1;
	}
	do_keyboard_interrupt();
	cli();
	if (inb_p(0x64) & 0x01)
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
		__asm__("\tlidt _no_idt"::);
	}
}

unsigned long kbd_init(unsigned long kmem_start)
{
	int i;
	struct kbd_struct * kbd;

	kbd = kbd_table + 0;
	for (i = 0 ; i < NR_CONSOLES ; i++,kbd++) {
		kbd->flags = KBD_DEFFLAGS;
		kbd->default_flags = KBD_DEFFLAGS;
	}
	bh_base[KEYBOARD_BH].routine = kbd_bh;
	request_irq(KEYBOARD_IRQ,keyboard_interrupt);
	mark_bh(KEYBOARD_BH);
	return kmem_start;
}
