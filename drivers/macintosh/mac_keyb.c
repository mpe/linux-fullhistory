/*
 * drivers/char/mac_keyb.c
 *
 * Keyboard driver for Power Macintosh computers.
 *
 * Adapted from drivers/char/keyboard.c by Paul Mackerras
 * (see that file for its authors and contributors).
 *
 * Copyright (C) 1996 Paul Mackerras.
 */

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/tty_flip.h>
#include <linux/config.h>

#include <asm/bitops.h>
#include <asm/adb.h>
#include <asm/cuda.h>
#include <asm/init.h>

#include <linux/kbd_kern.h>
#include <linux/kbd_ll.h>

#define KEYB_KEYREG	0	/* register # for key up/down data */
#define KEYB_LEDREG	2	/* register # for leds on ADB keyboard */
#define MOUSE_DATAREG	0	/* reg# for movement/button codes from mouse */

static u_short macplain_map[NR_KEYS] __initdata = {
	0xfb61,	0xfb73,	0xfb64,	0xfb66,	0xfb68,	0xfb67,	0xfb7a,	0xfb78,
	0xfb63,	0xfb76,	0xf200,	0xfb62,	0xfb71,	0xfb77,	0xfb65,	0xfb72,
	0xfb79,	0xfb74,	0xf031,	0xf032,	0xf033,	0xf034,	0xf036,	0xf035,
	0xf03d,	0xf039,	0xf037,	0xf02d,	0xf038,	0xf030,	0xf05d,	0xfb6f,
	0xfb75,	0xf05b,	0xfb69,	0xfb70,	0xf201,	0xfb6c,	0xfb6a,	0xf027,
	0xfb6b,	0xf03b,	0xf05c,	0xf02c,	0xf02f,	0xfb6e,	0xfb6d,	0xf02e,
	0xf009,	0xf020,	0xf060,	0xf07f,	0xf200,	0xf01b,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf200,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf104,	0xf105,	0xf106,	0xf102,	0xf107,	0xf108,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf209,	0xf200,	0xf109,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf103,	0xf117,
	0xf101,	0xf119,	0xf100,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static u_short macshift_map[NR_KEYS] __initdata = {
	0xfb41,	0xfb53,	0xfb44,	0xfb46,	0xfb48,	0xfb47,	0xfb5a,	0xfb58,
	0xfb43,	0xfb56,	0xf200,	0xfb42,	0xfb51,	0xfb57,	0xfb45,	0xfb52,
	0xfb59,	0xfb54,	0xf021,	0xf040,	0xf023,	0xf024,	0xf05e,	0xf025,
	0xf02b,	0xf028,	0xf026,	0xf05f,	0xf02a,	0xf029,	0xf07d,	0xfb4f,
	0xfb55,	0xf07b,	0xfb49,	0xfb50,	0xf201,	0xfb4c,	0xfb4a,	0xf022,
	0xfb4b,	0xf03a,	0xf07c,	0xf03c,	0xf03f,	0xfb4e,	0xfb4d,	0xf03e,
	0xf009,	0xf020,	0xf07e,	0xf07f,	0xf200,	0xf01b,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf200,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf10e,	0xf10f,	0xf110,	0xf10c,	0xf111,	0xf112,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf203,	0xf200,	0xf113,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf20b,	0xf116,	0xf10d,	0xf117,
	0xf10b,	0xf20a,	0xf10a,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static u_short macaltgr_map[NR_KEYS] __initdata = {
	0xf914,	0xfb73,	0xf917,	0xf919,	0xfb68,	0xfb67,	0xfb7a,	0xfb78,
	0xf916,	0xfb76,	0xf200,	0xf915,	0xfb71,	0xfb77,	0xf918,	0xfb72,
	0xfb79,	0xfb74,	0xf200,	0xf040,	0xf200,	0xf024,	0xf200,	0xf200,
	0xf200,	0xf05d,	0xf07b,	0xf05c,	0xf05b,	0xf07d,	0xf07e,	0xfb6f,
	0xfb75,	0xf200,	0xfb69,	0xfb70,	0xf201,	0xfb6c,	0xfb6a,	0xf200,
	0xfb6b,	0xf200,	0xf200,	0xf200,	0xf200,	0xfb6e,	0xfb6d,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf90a,	0xf90b,	0xf90c,	0xf90d,	0xf90e,	0xf90f,
	0xf910,	0xf911,	0xf200,	0xf912,	0xf913,	0xf200,	0xf200,	0xf200,
	0xf510,	0xf511,	0xf512,	0xf50e,	0xf513,	0xf514,	0xf200,	0xf516,
	0xf200,	0xf10c,	0xf200,	0xf202,	0xf200,	0xf515,	0xf200,	0xf517,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf50f,	0xf117,
	0xf50d,	0xf119,	0xf50c,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static u_short macctrl_map[NR_KEYS] __initdata = {
	0xf001,	0xf013,	0xf004,	0xf006,	0xf008,	0xf007,	0xf01a,	0xf018,
	0xf003,	0xf016,	0xf200,	0xf002,	0xf011,	0xf017,	0xf005,	0xf012,
	0xf019,	0xf014,	0xf200,	0xf000,	0xf01b,	0xf01c,	0xf01e,	0xf01d,
	0xf200,	0xf200,	0xf01f,	0xf01f,	0xf07f,	0xf200,	0xf01d,	0xf00f,
	0xf015,	0xf01b,	0xf009,	0xf010,	0xf201,	0xf00c,	0xf00a,	0xf007,
	0xf00b,	0xf200,	0xf01c,	0xf200,	0xf07f,	0xf00e,	0xf00d,	0xf20e,
	0xf200,	0xf000,	0xf000,	0xf008,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf200,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf104,	0xf105,	0xf106,	0xf102,	0xf107,	0xf108,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf204,	0xf200,	0xf109,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf103,	0xf117,
	0xf101,	0xf119,	0xf100,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static u_short macshift_ctrl_map[NR_KEYS] __initdata = {
	0xf001,	0xf013,	0xf004,	0xf006,	0xf008,	0xf007,	0xf01a,	0xf018,
	0xf003,	0xf016,	0xf200,	0xf002,	0xf011,	0xf017,	0xf005,	0xf012,
	0xf019,	0xf014,	0xf200,	0xf000,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf01f,	0xf200,	0xf200,	0xf200,	0xf00f,
	0xf015,	0xf200,	0xf009,	0xf010,	0xf201,	0xf00c,	0xf00a,	0xf200,
	0xf00b,	0xf200,	0xf200,	0xf200,	0xf200,	0xf00e,	0xf00d,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf200,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf10c,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf200,	0xf117,
	0xf200,	0xf119,	0xf200,	0xf700,	0xf701,	0xf702,	0xf200,	0xf20c,
};

static u_short macalt_map[NR_KEYS] __initdata = {
	0xf861,	0xf873,	0xf864,	0xf866,	0xf868,	0xf867,	0xf87a,	0xf878,
	0xf863,	0xf876,	0xf200,	0xf862,	0xf871,	0xf877,	0xf865,	0xf872,
	0xf879,	0xf874,	0xf831,	0xf832,	0xf833,	0xf834,	0xf836,	0xf835,
	0xf83d,	0xf839,	0xf837,	0xf82d,	0xf838,	0xf830,	0xf85d,	0xf86f,
	0xf875,	0xf85b,	0xf869,	0xf870,	0xf80d,	0xf86c,	0xf86a,	0xf827,
	0xf86b,	0xf83b,	0xf85c,	0xf82c,	0xf82f,	0xf86e,	0xf86d,	0xf82e,
	0xf809,	0xf820,	0xf860,	0xf87f,	0xf200,	0xf81b,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf210,	0xf211,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf900,	0xf901,	0xf902,	0xf903,	0xf904,	0xf905,
	0xf906,	0xf907,	0xf200,	0xf908,	0xf909,	0xf200,	0xf200,	0xf200,
	0xf504,	0xf505,	0xf506,	0xf502,	0xf507,	0xf508,	0xf200,	0xf50a,
	0xf200,	0xf10c,	0xf200,	0xf209,	0xf200,	0xf509,	0xf200,	0xf50b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf503,	0xf117,
	0xf501,	0xf119,	0xf500,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static u_short macctrl_alt_map[NR_KEYS] __initdata = {
	0xf801,	0xf813,	0xf804,	0xf806,	0xf808,	0xf807,	0xf81a,	0xf818,
	0xf803,	0xf816,	0xf200,	0xf802,	0xf811,	0xf817,	0xf805,	0xf812,
	0xf819,	0xf814,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf80f,
	0xf815,	0xf200,	0xf809,	0xf810,	0xf201,	0xf80c,	0xf80a,	0xf200,
	0xf80b,	0xf200,	0xf200,	0xf200,	0xf200,	0xf80e,	0xf80d,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf200,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf504,	0xf505,	0xf506,	0xf502,	0xf507,	0xf508,	0xf200,	0xf50a,
	0xf200,	0xf10c,	0xf200,	0xf200,	0xf200,	0xf509,	0xf200,	0xf50b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf503,	0xf117,
	0xf501,	0xf119,	0xf500,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};


static void kbd_repeat(unsigned long);
static struct timer_list repeat_timer = { NULL, NULL, 0, 0, kbd_repeat };
static int last_keycode;

static void keyboard_input(unsigned char *, int, struct pt_regs *, int);
static void input_keycode(int, int);
static void leds_done(struct adb_request *);
static void mac_put_queue(int);

#ifdef CONFIG_ADBMOUSE
/* XXX: Hook for mouse driver */
void (*adb_mouse_interrupt_hook)(unsigned char *, int);
int adb_emulate_buttons = 0;
int adb_button2_keycode = 0x7d;	/* right control key */
int adb_button3_keycode = 0x7c; /* right option key */
#endif

extern int console_loglevel;

extern struct kbd_struct kbd_table[];
extern struct wait_queue * keypress_wait;

extern void handle_scancode(unsigned char);

static struct adb_ids keyboard_ids;
static struct adb_ids mouse_ids;

/* this map indicates which keys shouldn't autorepeat. */
static unsigned char dont_repeat[128] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,	/* esc...option */
	0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, /* num lock */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, /* scroll lock */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

__openfirmware

int mackbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	return -EINVAL;
}

int mackbd_getkeycode(unsigned int scancode)
{
	return -EINVAL;
}

int mackbd_pretranslate(unsigned char scancode, char raw_mode)
{
	return 1;
}

int mackbd_translate(unsigned char keycode, unsigned char *keycodep,
		     char raw_mode)
{
	if (!raw_mode) {
		/*
		 * Convert R-shift/control/option to L version.
		 */
		switch (keycode) {
		case 0x7b: keycode = 0x38; break; /* R-shift */
		case 0x7c: keycode = 0x3a; break; /* R-option */
		case 0x7d: keycode = 0x36; break; /* R-control */
		}
	}
	*keycodep = keycode;
	return 1;
}

int mackbd_unexpected_up(unsigned char keycode)
{
	return 0x80;
}

static void
keyboard_input(unsigned char *data, int nb, struct pt_regs *regs, int apoll)
{
	/* first check this is from register 0 */
	if (nb != 3 || (data[0] & 3) != KEYB_KEYREG)
		return;		/* ignore it */
	kbd_pt_regs = regs;
	input_keycode(data[1], 0);
	if (!(data[2] == 0xff || (data[2] == 0x7f && data[1] == 0x7f)))
		input_keycode(data[2], 0);
}

static void
input_keycode(int keycode, int repeat)
{
	struct kbd_struct *kbd;
	int up_flag;

 	kbd = kbd_table + fg_console;
	up_flag = (keycode & 0x80);
	keycode &= 0x7f;

	/* on the powerbook 3400, the power key gives code 0x7e */
	if (keycode == 0x7e)
		keycode = 0x7f;

	if (!repeat)
		del_timer(&repeat_timer);

#ifdef CONFIG_ADBMOUSE
	/*
	 * XXX: Add mouse button 2+3 fake codes here if mouse open.
	 *	Keep track of 'button' states here as we only send 
	 *	single up/down events!
	 *	Really messy; might need to check if keyboard is in
	 *	VC_RAW mode.
	 *	Might also want to know how many buttons need to be emulated.
	 *	-> hide this as function in arch/m68k/mac ?
	 */
	if (adb_emulate_buttons
	    && (keycode == adb_button2_keycode
		|| keycode == adb_button3_keycode)
	    && (adb_mouse_interrupt_hook || console_loglevel == 10)) {
		int button;
		/* faked ADB packet */
		static unsigned char data[4] = { 0, 0x80, 0x80, 0x80 };

		button = keycode == adb_button2_keycode? 2: 3;
		if (data[button] != up_flag) {
			/* send a fake mouse packet */
			data[button] = up_flag;
			if (console_loglevel >= 8)
				printk("fake mouse event: %x %x %x\n",
				       data[1], data[2], data[3]);
			if (adb_mouse_interrupt_hook)
				adb_mouse_interrupt_hook(data, 4);
		}
		return;
	}
#endif /* CONFIG_ADBMOUSE */

	if (kbd->kbdmode != VC_RAW) {
		if (!up_flag && !dont_repeat[keycode]) {
			last_keycode = keycode;
			repeat_timer.expires = jiffies + (repeat? HZ/15: HZ/2);
			add_timer(&repeat_timer);
		}

		/*
		 * adb kludge!! Imitate pc caps lock behaviour by
		 * generating an up/down event for each time caps
		 * is pressed/released. Also, makes sure that the
		 * LED are handled.  atong@uiuc.edu
		 */
		 switch (keycode) {
		 /*case 0xb9:*/
		 case 0x39:
			handle_scancode(0x39);
			handle_scancode(0xb9);
		 	mark_bh(KEYBOARD_BH);
		 	return;
		 case 0x47:
		 /*case 0xc7:*/
		 	mark_bh(KEYBOARD_BH);
		 	break;
		 }
	}

	handle_scancode(keycode + up_flag);
}

static void
kbd_repeat(unsigned long xxx)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	input_keycode(last_keycode, 1);
	restore_flags(flags);
}

static void mac_put_queue(int ch)
{
	extern struct tty_driver console_driver;
	struct tty_struct *tty;

	tty = console_driver.table? console_driver.table[fg_console]: NULL;
	wake_up(&keypress_wait);
	if (tty) {
		tty_insert_flip_char(tty, ch, 0);
		con_schedule_flip(tty);
	}
}

#ifdef CONFIG_ADBMOUSE
static void
mouse_input(unsigned char *data, int nb, struct pt_regs *regs, int autopoll)
{
  /* [ACA:23-Mar-97] Three button mouse support.  This is designed to
     function with MkLinux DR-2.1 style X servers.  It only works with
     three-button mice that conform to Apple's multi-button mouse
     protocol. */

  /*
    The X server for MkLinux DR2.1 uses the following unused keycodes to
    read the mouse:

    0x7e  This indicates that the next two keycodes should be interpreted
          as mouse information.  The first following byte's high bit
          represents the state of the left button.  The lower seven bits
          represent the x-axis acceleration.  The lower seven bits of the
          second byte represent y-axis acceleration.

    0x3f  The x server interprets this keycode as a middle button
          release.

    0xbf  The x server interprets this keycode as a middle button
          depress.

    0x40  The x server interprets this keycode as a right button
          release.

    0xc0  The x server interprets this keycode as a right button
          depress.

    NOTES: There should be a better way of handling mice in the X server.
    The MOUSE_ESCAPE code (0x7e) should be followed by three bytes instead
    of two.  The three mouse buttons should then, in the X server, be read
    as the high-bits of all three bytes.  The x and y motions can still be
    in the first two bytes.  Maybe I'll do this...
  */

  /*
    Handler 1 -- 100cpi original Apple mouse protocol.
    Handler 2 -- 200cpi original Apple mouse protocol.

    For Apple's standard one-button mouse protocol the data array will
    contain the following values:

                BITS    COMMENTS
    data[0] = dddd 1100 ADB command: Talk, register 0, for device dddd.
    data[1] = bxxx xxxx First button and x-axis motion.
    data[2] = byyy yyyy Second button and y-axis motion.

    Handler 4 -- Apple Extended mouse protocol.

    For Apple's 3-button mouse protocol the data array will contain the
    following values:

		BITS    COMMENTS
    data[0] = dddd 1100 ADB command: Talk, register 0, for device dddd.
    data[1] = bxxx xxxx Left button and x-axis motion.
    data[2] = byyy yyyy Second button and y-axis motion.
    data[3] = byyy bxxx Third button and fourth button.  Y is additional
	      high bits of y-axis motion.  XY is additional
	      high bits of x-axis motion.
  */
	struct kbd_struct *kbd;

	if (adb_mouse_interrupt_hook)
		adb_mouse_interrupt_hook(data, nb);

	kbd = kbd_table + fg_console;

	/* Only send mouse codes when keyboard is in raw mode. */
	if (kbd->kbdmode == VC_RAW) {
		static unsigned char uch_ButtonStateSecond = 0x80;
		unsigned char uchButtonSecond;

		/* Send first button, second button and movement. */
		mac_put_queue(0x7e);
		mac_put_queue(data[1]);
		mac_put_queue(data[2]);

		/* [ACA: Are there any two-button ADB mice that use handler 1 or 2?] */

		/* Store the button state. */
		uchButtonSecond = (data[2] & 0x80);

		/* Send second button. */
		if (uchButtonSecond != uch_ButtonStateSecond) {
			mac_put_queue(0x3f | uchButtonSecond);
			uch_ButtonStateSecond = uchButtonSecond;
		}

		/* Macintosh 3-button mouse (handler 4). */
		if (nb == 4) {
			static unsigned char uch_ButtonStateThird = 0x80;
			unsigned char uchButtonThird;

			/* Store the button state for speed. */
			uchButtonThird = (data[3] & 0x80);

			/* Send third button. */
			if (uchButtonThird != uch_ButtonStateThird) {
				mac_put_queue(0x40 | uchButtonThird);
				uch_ButtonStateThird = uchButtonThird;
			}
		}
	}
}
#endif /* CONFIG_ADBMOUSE */

/* Map led flags as defined in kbd_kern.h to bits for Apple keyboard. */
static unsigned char mac_ledmap[8] = {
    0,		/* none */
    4,		/* scroll lock */
    1,		/* num lock */
    5,		/* scroll + num lock */
    2,		/* caps lock */
    6,		/* caps + scroll lock */
    3,		/* caps + num lock */
    7,		/* caps + num + scroll lock */
};

static struct adb_request led_request;
static int leds_pending[16];
static int pending_devs[16];
static int pending_led_start=0;
static int pending_led_end=0;

static void real_mackbd_leds(unsigned char leds, int device)
{

    if (led_request.complete) {
	adb_request(&led_request, leds_done, 0, 3,
		    ADB_WRITEREG(device, KEYB_LEDREG), 0xff,
		    ~mac_ledmap[leds]);
    } else {
	if (!(leds_pending[device] & 0x100)) {
	    pending_devs[pending_led_end] = device;
	    pending_led_end++;
	    pending_led_end = (pending_led_end < 16) ? pending_led_end : 0;
	}
	leds_pending[device] = leds | 0x100;
    }
}

void mackbd_leds(unsigned char leds)
{
    int i;

    for(i = 0; i < keyboard_ids.nids; i++)
	real_mackbd_leds(leds,keyboard_ids.id[i]);
}

static void leds_done(struct adb_request *req)
{
    int leds,device;

    if (pending_led_start != pending_led_end) {
	device = pending_devs[pending_led_start];
	leds = leds_pending[device] & 0xff;
	leds_pending[device] = 0;
	pending_led_start++;
	pending_led_start = (pending_led_start < 16) ? pending_led_start : 0;
	real_mackbd_leds(leds,device);
    }

}

__initfunc(void mackbd_init_hw(void))
{
	struct adb_request req;
	int i;

	if ( (_machine != _MACH_chrp) && (_machine != _MACH_Pmac) )
	    return;

	/* setup key map */
	memcpy(key_maps[0], macplain_map, sizeof(plain_map));
	memcpy(key_maps[1], macshift_map, sizeof(plain_map));
	memcpy(key_maps[2], macaltgr_map, sizeof(plain_map));
	memcpy(key_maps[4], macctrl_map, sizeof(plain_map));
	memcpy(key_maps[5], macshift_ctrl_map, sizeof(plain_map));
	memcpy(key_maps[8], macalt_map, sizeof(plain_map));
	memcpy(key_maps[12], macctrl_alt_map, sizeof(plain_map));

#ifdef CONFIG_ADBMOUSE
	/* initialize mouse interrupt hook */
	adb_mouse_interrupt_hook = NULL;

	adb_register(ADB_MOUSE, 1, &mouse_ids, mouse_input);
#endif /* CONFIG_ADBMOUSE */

	adb_register(ADB_KEYBOARD, 5, &keyboard_ids, keyboard_input);

	for(i = 0; i < keyboard_ids.nids; i++) {
	    /* turn off all leds */
	    adb_request(&req, NULL, ADBREQ_SYNC, 3,
	    ADB_WRITEREG(keyboard_ids.id[i], KEYB_LEDREG), 0xff, 0xff);
	}

	/* get the keyboard to send separate codes for
	   left and right shift, control, option keys. */
	for(i = 0;i < keyboard_ids.nids; i++) {
	    /* get the keyboard to send separate codes for
	    left and right shift, control, option keys. */
	    adb_request(&req, NULL, ADBREQ_SYNC, 3,
	    ADB_WRITEREG(keyboard_ids.id[i], 3), 0, 3);
	}

	led_request.complete = 1;

	/* Try to switch the mouse (id 3) to handler 4, for three-button
	   mode. (0x20 is Service Request Enable, 0x03 is Device ID). */
	for(i = 0; i < mouse_ids.nids; i++) {
	    adb_request(&req, NULL, ADBREQ_SYNC | ADBREQ_REPLY, 1,
			ADB_READREG(mouse_ids.id[i], 1));

	    if ((req.reply_len) &&
		(req.reply[1] == 0x9a) && (req.reply[2] == 0x21)) {

		printk("aha, trackball found at %d\n", mouse_ids.id[i]);

		adb_request(&req, NULL, ADBREQ_SYNC, 3,
		ADB_WRITEREG(mouse_ids.id[i], 3), 0x63, 4 );

		adb_request(&req, NULL, ADBREQ_SYNC, 3,
		ADB_WRITEREG(mouse_ids.id[i],1), 00,0x81);

		adb_request(&req, NULL, ADBREQ_SYNC, 3,
		ADB_WRITEREG(mouse_ids.id[i],1), 01,0x81);

		adb_request(&req, NULL, ADBREQ_SYNC, 3,
		ADB_WRITEREG(mouse_ids.id[i],1), 02,0x81);

		adb_request(&req, NULL, ADBREQ_SYNC, 3,
		ADB_WRITEREG(mouse_ids.id[i],1), 03,0x38);

		adb_request(&req, NULL, ADBREQ_SYNC, 3,
		ADB_WRITEREG(mouse_ids.id[i],1), 00,0x81);

		adb_request(&req, NULL, ADBREQ_SYNC, 3,
		ADB_WRITEREG(mouse_ids.id[i],1), 01,0x81);

		adb_request(&req, NULL, ADBREQ_SYNC, 3,
		ADB_WRITEREG(mouse_ids.id[i],1), 02,0x81);

		adb_request(&req, NULL, ADBREQ_SYNC, 3,
		ADB_WRITEREG(mouse_ids.id[i],1), 03,0x38);
	    }
	}
}
