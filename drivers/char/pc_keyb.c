/*
 * linux/drivers/char/pc_keyb.c
 *
 * Separation of the PC low-level part by Geert Uytterhoeven, May 1997
 * See keyboard.c for the whole history.
 *
 * Major cleanup by Martin Mares, May 1997
 */

#include <linux/config.h>

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/delay.h>

#include <asm/keyboard.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/irq.h>

/* Some configuration switches are present in the include file... */

#include "pc_keyb.h"

/* Simple translation table for the SysRq keys */

#ifdef CONFIG_MAGIC_SYSRQ
unsigned char pckbd_sysrq_xlate[128] =
	"\000\0331234567890-=\177\t"			/* 0x00 - 0x0f */
	"qwertyuiop[]\r\000as"				/* 0x10 - 0x1f */
	"dfghjkl;'`\000\\zxcv"				/* 0x20 - 0x2f */
	"bnm,./\000*\000 \000\201\202\203\204\205"	/* 0x30 - 0x3f */
	"\206\207\210\211\212\000\000789-456+1"		/* 0x40 - 0x4f */
	"230\177\000\000\213\214\000\000\000\000\000\000\000\000\000\000" /* 0x50 - 0x5f */
	"\r\000/";					/* 0x60 - 0x6f */
#endif

/*
 * In case we run on a non-x86 hardware we need to initialize both the keyboard
 * controller and the keyboard. On a x86, the BIOS will already have initialized
 * them.
 */

/*
 * Some x86 BIOSes do not correctly initialize the keyboard, so the
 * "kbd-reset" command line options can be given to force a reset.
 * [Ranger]
 */
#ifdef __i386__
 int kbd_startup_reset __initdata = 0;
#else
 int kbd_startup_reset __initdata = 1;
#endif

__initfunc(static int kbd_wait_for_input(void))
{
	int     status, data;
	unsigned long start = jiffies;

	do {
                status = inb(KBD_STATUS_REG);

                /*
                 * Wait for input data to become available.  This bit will
                 * then be cleared by the following read of the DATA
                 * register.
                 */
                if (!(status & KBD_STAT_OBF))
			continue;

		data = inb(KBD_DATA_REG);

                /*
                 * Check to see if a timeout error has occurred.  This means
                 * that transmission was started but did not complete in the
                 * normal time cycle.  PERR is set when a parity error occurred
                 * in the last transmission.
                 */
                if (status & (KBD_STAT_GTO | KBD_STAT_PERR)) {
			continue;
                }
		return (data & 0xff);
        } while (jiffies - start < KBD_INIT_TIMEOUT);
        return -1;	/* timed-out if fell through to here... */
}

__initfunc(static void kbd_write(int address, int data))
{
	int status;

	do {
		status = inb(KBD_STATUS_REG);
	} while (status & KBD_STAT_IBF);
	outb(data, address);
}

__initfunc(static char *initialize_kbd2(void))
{
	int status;

	/* Flush any pending input. */

	while (kbd_wait_for_input() != -1)
		;

	/*
	 * Test the keyboard interface.
	 * This seems to be the only way to get it going.
	 * If the test is successful a x55 is placed in the input buffer.
	 */

	kbd_write(KBD_CNTL_REG, KBD_CCMD_SELF_TEST);
	if (kbd_wait_for_input() != 0x55)
		return "Keyboard failed self test";

	/*
	 * Perform a keyboard interface test.  This causes the controller
	 * to test the keyboard clock and data lines.  The results of the
	 * test are placed in the input buffer.
	 */

	kbd_write(KBD_CNTL_REG, KBD_CCMD_KBD_TEST);
	if (kbd_wait_for_input() != 0x00)
		return "Keyboard interface failed self test";

	/* Enable the keyboard by allowing the keyboard clock to run. */

	kbd_write(KBD_CNTL_REG, KBD_CCMD_KBD_ENABLE);

	/*
	 * Reset keyboard. If the read times out
	 * then the assumption is that no keyboard is
	 * plugged into the machine.
	 * This defaults the keyboard to scan-code set 2.
	 *
	 * Set up to try again if the keyboard asks for RESEND.
	 */

        do {
		kbd_write(KBD_DATA_REG, KBD_CMD_RESET);
                status = kbd_wait_for_input();
                if (status == KBD_REPLY_ACK)
			break;
                else if (status != KBD_REPLY_RESEND)
			return "Keyboard reset failed, no ACK";
        } while (1);

	if (kbd_wait_for_input() != KBD_REPLY_POR)
		return "Keyboard reset failed, no POR";

	/*
	 * Set keyboard controller mode. During this, the keyboard should be
	 * in the disabled state.
	 *
	 * Set up to try again if the keyboard asks for RESEND.
	 */

	do {
		kbd_write(KBD_DATA_REG, KBD_CMD_DISABLE);
		status = kbd_wait_for_input();
		if (status == KBD_REPLY_ACK)
			break;
		else if (status != KBD_REPLY_RESEND)
			return "Disable keyboard: no ACK";
	} while (1);

	kbd_write(KBD_CNTL_REG, KBD_CCMD_WRITE_MODE);
	kbd_write(KBD_DATA_REG, KBD_MODE_KBD_INT
		              | KBD_MODE_SYS
		              | KBD_MODE_DISABLE_MOUSE
		              | KBD_MODE_KCC);

	/* ibm powerpc portables need this to use scan-code set 1 -- Cort */
	kbd_write(KBD_CNTL_REG, KBD_CCMD_READ_MODE);
	if (!(kbd_wait_for_input() & KBD_MODE_KCC)) {
		/*
		 * If the controller does not support conversion,
		 * Set the keyboard to scan-code set 1.
		 */
		kbd_write(KBD_DATA_REG, 0xF0);
		kbd_wait_for_input();
		kbd_write(KBD_DATA_REG, 0x01);
		kbd_wait_for_input();
	}

	
	kbd_write(KBD_DATA_REG, KBD_CMD_ENABLE);
	if (kbd_wait_for_input() != KBD_REPLY_ACK)
		return "Enable keyboard: no ACK";

	/*
	 * Finally, set the typematic rate to maximum.
	 */

	kbd_write(KBD_DATA_REG, KBD_CMD_SET_RATE);
	if (kbd_wait_for_input() != KBD_REPLY_ACK)
		return "Set rate: no ACK";
	kbd_write(KBD_DATA_REG, 0x00);
	if (kbd_wait_for_input() != KBD_REPLY_ACK)
		return "Set rate: no ACK";

	return NULL;
}

__initfunc(static void initialize_kbd(void))
{
	char *msg;

	disable_irq(KEYBOARD_IRQ);
	msg = initialize_kbd2();
	enable_irq(KEYBOARD_IRQ);

	if (msg)
		printk(KERN_WARNING "initialize_kbd: %s\n", msg);
}



unsigned char pckbd_read_mask = KBD_STAT_OBF; /* Modified by psaux.c */

/* used only by send_data - set by keyboard_interrupt */
static volatile unsigned char reply_expected = 0;
static volatile unsigned char acknowledge = 0;
static volatile unsigned char resend = 0;

/*
 *	Wait for keyboard controller input buffer is empty.
 *
 *	Don't use 'jiffies' so that we don't depend on
 *	interrupts..
 */

static inline void kb_wait(void)
{
	unsigned long timeout = KBC_TIMEOUT;

	do {
		if (! (inb_p(KBD_STATUS_REG) & KBD_STAT_IBF))
			return;
		mdelay(1);
		timeout--;
	} while (timeout);
#ifdef KBD_REPORT_TIMEOUTS
	printk(KERN_WARNING "Keyboard timed out\n");
#endif
}

/*
 * Translation of escaped scancodes to keycodes.
 * This is now user-settable.
 * The keycodes 1-88,96-111,119 are fairly standard, and
 * should probably not be changed - changing might confuse X.
 * X also interprets scancode 0x5d (KEY_Begin).
 *
 * For 1-88 keycode equals scancode.
 */

#define E0_KPENTER 96
#define E0_RCTRL   97
#define E0_KPSLASH 98
#define E0_PRSCR   99
#define E0_RALT    100
#define E0_BREAK   101  /* (control-pause) */
#define E0_HOME    102
#define E0_UP      103
#define E0_PGUP    104
#define E0_LEFT    105
#define E0_RIGHT   106
#define E0_END     107
#define E0_DOWN    108
#define E0_PGDN    109
#define E0_INS     110
#define E0_DEL     111

#define E1_PAUSE   119

/*
 * The keycodes below are randomly located in 89-95,112-118,120-127.
 * They could be thrown away (and all occurrences below replaced by 0),
 * but that would force many users to use the `setkeycodes' utility, where
 * they needed not before. It does not matter that there are duplicates, as
 * long as no duplication occurs for any single keyboard.
 */
#define SC_LIM 89

#define FOCUS_PF1 85           /* actual code! */
#define FOCUS_PF2 89
#define FOCUS_PF3 90
#define FOCUS_PF4 91
#define FOCUS_PF5 92
#define FOCUS_PF6 93
#define FOCUS_PF7 94
#define FOCUS_PF8 95
#define FOCUS_PF9 120
#define FOCUS_PF10 121
#define FOCUS_PF11 122
#define FOCUS_PF12 123

#define JAP_86     124
/* tfj@olivia.ping.dk:
 * The four keys are located over the numeric keypad, and are
 * labelled A1-A4. It's an rc930 keyboard, from
 * Regnecentralen/RC International, Now ICL.
 * Scancodes: 59, 5a, 5b, 5c.
 */
#define RGN1 124
#define RGN2 125
#define RGN3 126
#define RGN4 127

static unsigned char high_keys[128 - SC_LIM] = {
  RGN1, RGN2, RGN3, RGN4, 0, 0, 0,                   /* 0x59-0x5f */
  0, 0, 0, 0, 0, 0, 0, 0,                            /* 0x60-0x67 */
  0, 0, 0, 0, 0, FOCUS_PF11, 0, FOCUS_PF12,          /* 0x68-0x6f */
  0, 0, 0, FOCUS_PF2, FOCUS_PF9, 0, 0, FOCUS_PF3,    /* 0x70-0x77 */
  FOCUS_PF4, FOCUS_PF5, FOCUS_PF6, FOCUS_PF7,        /* 0x78-0x7b */
  FOCUS_PF8, JAP_86, FOCUS_PF10, 0                   /* 0x7c-0x7f */
};

/* BTC */
#define E0_MACRO   112
/* LK450 */
#define E0_F13     113
#define E0_F14     114
#define E0_HELP    115
#define E0_DO      116
#define E0_F17     117
#define E0_KPMINPLUS 118
/*
 * My OmniKey generates e0 4c for  the "OMNI" key and the
 * right alt key does nada. [kkoller@nyx10.cs.du.edu]
 */
#define E0_OK	124
/*
 * New microsoft keyboard is rumoured to have
 * e0 5b (left window button), e0 5c (right window button),
 * e0 5d (menu button). [or: LBANNER, RBANNER, RMENU]
 * [or: Windows_L, Windows_R, TaskMan]
 */
#define E0_MSLW	125
#define E0_MSRW	126
#define E0_MSTM	127

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
  E0_UP, E0_PGUP, 0, E0_LEFT, E0_OK, E0_RIGHT, E0_KPMINPLUS, E0_END,/* 0x48-0x4f */
  E0_DOWN, E0_PGDN, E0_INS, E0_DEL, 0, 0, 0, 0,	      /* 0x50-0x57 */
  0, 0, 0, E0_MSLW, E0_MSRW, E0_MSTM, 0, 0,	      /* 0x58-0x5f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x60-0x67 */
  0, 0, 0, 0, 0, 0, 0, E0_MACRO,		      /* 0x68-0x6f */
  0, 0, 0, 0, 0, 0, 0, 0,			      /* 0x70-0x77 */
  0, 0, 0, 0, 0, 0, 0, 0			      /* 0x78-0x7f */
};

static unsigned int prev_scancode = 0;   /* remember E0, E1 */

int pckbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	if (scancode < SC_LIM || scancode > 255 || keycode > 127)
	  return -EINVAL;
	if (scancode < 128)
	  high_keys[scancode - SC_LIM] = keycode;
	else
	  e0_keys[scancode - 128] = keycode;
	return 0;
}

int pckbd_getkeycode(unsigned int scancode)
{
	return
	  (scancode < SC_LIM || scancode > 255) ? -EINVAL :
	  (scancode < 128) ? high_keys[scancode - SC_LIM] :
	    e0_keys[scancode - 128];
}

#if DISABLE_KBD_DURING_INTERRUPTS
static inline void send_cmd(unsigned char c)
{
	kb_wait();
	outb(c, KBD_CNTL_REG);
}

#define disable_keyboard()	do { send_cmd(KBD_CCMD_KBD_DISABLE); kb_wait(); } while (0)
#define enable_keyboard()	send_cmd(KBD_CCMD_KBD_ENABLE)
#else
#define disable_keyboard()	/* nothing */
#define enable_keyboard()	/* nothing */
#endif

static int do_acknowledge(unsigned char scancode)
{
	if (reply_expected) {
	  /* Unfortunately, we must recognise these codes only if we know they
	   * are known to be valid (i.e., after sending a command), because there
	   * are some brain-damaged keyboards (yes, FOCUS 9000 again) which have
	   * keys with such codes :(
	   */
		if (scancode == KBD_REPLY_ACK) {
			acknowledge = 1;
			reply_expected = 0;
			return 0;
		} else if (scancode == KBD_REPLY_RESEND) {
			resend = 1;
			reply_expected = 0;
			return 0;
		}
		/* Should not happen... */
#if 0
		printk(KERN_DEBUG "keyboard reply expected - got %02x\n",
		       scancode);
#endif
	}
	if (scancode == 0) {
#ifdef KBD_REPORT_ERR
		printk(KERN_INFO "Keyboard buffer overflow\n");
#endif
		prev_scancode = 0;
		return 0;
	}
	return 1;
}

int pckbd_pretranslate(unsigned char scancode, char raw_mode)
{
	if (scancode == 0xff) {
	        /* in scancode mode 1, my ESC key generates 0xff */
		/* the calculator keys on a FOCUS 9000 generate 0xff */
#ifndef KBD_IS_FOCUS_9000
#ifdef KBD_REPORT_ERR
		if (!raw_mode)
		  printk(KERN_DEBUG "Keyboard error\n");
#endif
#endif
		prev_scancode = 0;
		return 0;
	}

	if (scancode == 0xe0 || scancode == 0xe1) {
		prev_scancode = scancode;
		return 0;
 	}
 	return 1;
}

int pckbd_translate(unsigned char scancode, unsigned char *keycode,
		    char raw_mode)
{
	if (prev_scancode) {
	  /*
	   * usually it will be 0xe0, but a Pause key generates
	   * e1 1d 45 e1 9d c5 when pressed, and nothing when released
	   */
	  if (prev_scancode != 0xe0) {
	      if (prev_scancode == 0xe1 && scancode == 0x1d) {
		  prev_scancode = 0x100;
		  return 0;
	      } else if (prev_scancode == 0x100 && scancode == 0x45) {
		  *keycode = E1_PAUSE;
		  prev_scancode = 0;
	      } else {
#ifdef KBD_REPORT_UNKN
		  if (!raw_mode)
		    printk(KERN_INFO "keyboard: unknown e1 escape sequence\n");
#endif
		  prev_scancode = 0;
		  return 0;
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
		return 0;

	      if (e0_keys[scancode])
		*keycode = e0_keys[scancode];
	      else {
#ifdef KBD_REPORT_UNKN
		  if (!raw_mode)
		    printk(KERN_INFO "keyboard: unknown scancode e0 %02x\n",
			   scancode);
#endif
		  return 0;
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

	  *keycode = high_keys[scancode - SC_LIM];

	  if (!*keycode) {
	      if (!raw_mode) {
#ifdef KBD_REPORT_UNKN
		  printk(KERN_INFO "keyboard: unrecognized scancode (%02x)"
			 " - ignored\n", scancode);
#endif
	      }
	      return 0;
	  }
 	} else
	  *keycode = scancode;
 	return 1;
}

char pckbd_unexpected_up(unsigned char keycode)
{
	/* unexpected, but this can happen: maybe this was a key release for a
	   FOCUS 9000 PF key; if we want to see it, we have to clear up_flag */
	if (keycode >= SC_LIM || keycode == 85)
	    return 0;
	else
	    return 0200;
}

static void keyboard_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned char status;

	kbd_pt_regs = regs;
	disable_keyboard();

	status = inb_p(KBD_STATUS_REG);
	do {
		unsigned char scancode;

		/* mouse data? */
		if (status & pckbd_read_mask & KBD_STAT_MOUSE_OBF)
			break;

		scancode = inb(KBD_DATA_REG);
		if ((status & KBD_STAT_OBF) && do_acknowledge(scancode))
			handle_scancode(scancode);

		status = inb(KBD_STATUS_REG);
	} while (status & KBD_STAT_OBF);

	mark_bh(KEYBOARD_BH);
	enable_keyboard();
}

/*
 * send_data sends a character to the keyboard and waits
 * for an acknowledge, possibly retrying if asked to. Returns
 * the success status.
 *
 * Don't use 'jiffies', so that we don't depend on interrupts
 */
static int send_data(unsigned char data)
{
	int retries = 3;

	do {
		unsigned long timeout = KBD_TIMEOUT;

		kb_wait();
		acknowledge = 0;
		resend = 0;
		reply_expected = 1;
		outb_p(data, KBD_DATA_REG);
		for (;;) {
			if (acknowledge)
				return 1;
			if (resend)
				break;
			mdelay(1);
			if (!--timeout) {
#ifdef KBD_REPORT_TIMEOUTS
				printk(KERN_WARNING "Keyboard timeout\n");
#endif
				return 0;
			}
		}
	} while (retries-- > 0);
#ifdef KBD_REPORT_TIMEOUTS
	printk(KERN_WARNING "keyboard: Too many NACKs -- noisy kbd cable?\n");
#endif
	return 0;
}

void pckbd_leds(unsigned char leds)
{
	if (!send_data(KBD_CMD_SET_LEDS) || !send_data(leds))
	    send_data(KBD_CMD_ENABLE);	/* re-enable kbd if any errors */
}

__initfunc(void pckbd_init_hw(void))
{
	request_irq(KEYBOARD_IRQ, keyboard_interrupt, 0, "keyboard", NULL);
	request_region(0x60, 16, "keyboard");
	if (kbd_startup_reset) initialize_kbd();
}

/* for "kbd-reset" cmdline param */
__initfunc(void kbd_reset_setup(char *str, int *ints))
{
	kbd_startup_reset = 1;
}
