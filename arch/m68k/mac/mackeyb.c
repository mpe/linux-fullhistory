/*
 * linux/arch/m68k/mac/mackeyb.c
 *
 * Keyboard driver for Macintosh computers.
 *
 * Adapted from drivers/macintosh/key_mac.c and arch/m68k/atari/akakeyb.c 
 * (see that file for its authors and contributors).
 *
 * Copyright (C) 1997 Michael Schmitz.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * misc. keyboard stuff (everything not in adb-bus.c or keyb_m68k.c)
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/init.h>
/* keyb */
#include <linux/keyboard.h>
#include <linux/random.h>
#include <linux/delay.h>
/* keyb */

#include <asm/setup.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>

#include <asm/macintosh.h>
#include <asm/macints.h>
/* for keyboard_input stuff */
#include <asm/adb.h>
#define KEYB_KEYREG	0	/* register # for key up/down data */
#define KEYB_LEDREG	2	/* register # for leds on ADB keyboard */
#define MOUSE_DATAREG	0	/* reg# for movement/button codes from mouse */
/* end keyboard_input stuff */

#include <linux/kbd_kern.h>
#include <linux/kbd_ll.h>

static void kbd_repeat(unsigned long);
static struct timer_list repeat_timer = { NULL, NULL, 0, 0, kbd_repeat };
static int last_keycode;

static void input_keycode(int, int);

extern struct kbd_struct kbd_table[];

extern void adb_bus_init(void);
extern void handle_scancode(unsigned char);
extern void put_queue(int);

/* keyb */
static void mac_leds_done(struct adb_request *);
static void keyboard_input(unsigned char *, int, struct pt_regs *);
static void mouse_input(unsigned char *, int, struct pt_regs *);

#ifdef CONFIG_ADBMOUSE
/* XXX: Hook for mouse driver */
void (*adb_mouse_interrupt_hook)(unsigned char *, int);
int adb_emulate_buttons = 0;
int adb_button2_keycode = 0x7d;	/* right control key */
int adb_button3_keycode = 0x7c; /* right option key */
#endif

/* The mouse driver - for debugging */
extern void adb_mouse_interrupt(char *, int);
/* end keyb */

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

/*
 * Mac private key maps
 */
u_short mac_plain_map[NR_KEYS] __initdata = {
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
	0xf306,	0xf307,	0xfb61,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf104,	0xf105,	0xf106,	0xf102,	0xf107,	0xf108,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf209,	0xf200,	0xf109,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf103,	0xf117,
	0xf101,	0xf119,	0xf100,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
};

u_short mac_shift_map[NR_KEYS] __initdata = {
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
	0xf306,	0xf307,	0xfb41,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf10e,	0xf10f,	0xf110,	0xf10c,	0xf111,	0xf112,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf203,	0xf200,	0xf113,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf20b,	0xf116,	0xf10d,	0xf117,
	0xf10b,	0xf20a,	0xf10a,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
};

u_short mac_altgr_map[NR_KEYS] __initdata = {
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
	0xf910,	0xf911,	0xf914,	0xf912,	0xf913,	0xf200,	0xf200,	0xf200,
	0xf510,	0xf511,	0xf512,	0xf50e,	0xf513,	0xf514,	0xf200,	0xf516,
	0xf200,	0xf10c,	0xf200,	0xf202,	0xf200,	0xf515,	0xf200,	0xf517,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf50f,	0xf117,
	0xf50d,	0xf119,	0xf50c,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
};

u_short mac_ctrl_map[NR_KEYS] __initdata = {
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
	0xf306,	0xf307,	0xf001,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf104,	0xf105,	0xf106,	0xf102,	0xf107,	0xf108,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf204,	0xf200,	0xf109,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf103,	0xf117,
	0xf101,	0xf119,	0xf100,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
};

u_short mac_shift_ctrl_map[NR_KEYS] __initdata = {
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
	0xf306,	0xf307,	0xf001,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf10c,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf200,	0xf117,
	0xf200,	0xf119,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf20c,
};

u_short mac_alt_map[NR_KEYS] __initdata = {
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
	0xf906,	0xf907,	0xf861,	0xf908,	0xf909,	0xf200,	0xf200,	0xf200,
	0xf504,	0xf505,	0xf506,	0xf502,	0xf507,	0xf508,	0xf200,	0xf50a,
	0xf200,	0xf10c,	0xf200,	0xf209,	0xf200,	0xf509,	0xf200,	0xf50b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf503,	0xf117,
	0xf501,	0xf119,	0xf500,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
};

u_short mac_ctrl_alt_map[NR_KEYS] __initdata = {
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
	0xf306,	0xf307,	0xf801,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf504,	0xf505,	0xf506,	0xf502,	0xf507,	0xf508,	0xf200,	0xf50a,
	0xf200,	0xf10c,	0xf200,	0xf200,	0xf200,	0xf509,	0xf200,	0xf50b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf503,	0xf117,
	0xf501,	0xf119,	0xf500,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
};

extern unsigned int keymap_count;

/*
 * Misc. defines for testing 
 */

extern int console_loglevel;

static struct adb_request led_request;
extern int in_keybinit;

/*
 * machdep keyboard routines, interface and key repeat method modeled after
 * drivers/macintosh/keyb_mac.c
 */

int mac_kbd_translate(unsigned char keycode, unsigned char *keycodep,
		     char raw_mode)
{
	if (!raw_mode) {
		/*
		 * Convert R-shift/control/option to L version.
		 * Remap keycode 0 (A) to the unused keycode 0x5a.
		 * Other parts of the system assume 0 is not a valid keycode.
		 */
		switch (keycode) {
		case 0x7b: keycode = 0x38; break; /* R-shift */
		case 0x7c: keycode = 0x3a; break; /* R-option */
		case 0x7d: keycode = 0x36; break; /* R-control */
		case 0:	   keycode = 0x5a; break; /* A */
		}
	}
	*keycodep = keycode;
	return 1;
}

int mac_kbd_unexpected_up(unsigned char keycode)
{
	return 0x80;
}

static void
keyboard_input(unsigned char *data, int nb, struct pt_regs *regs)
{
	/* first check this is from register 0 */
	if (nb != 5 || (data[2] & 3) != KEYB_KEYREG)
		return;		/* ignore it */
	kbd_pt_regs = regs;
	input_keycode(data[3], 0);
	if (!(data[4] == 0xff || (data[4] == 0x7f && data[3] == 0x7f)))
		input_keycode(data[4], 0);
}

static void
input_keycode(int keycode, int repeat)
{
	struct kbd_struct *kbd;
	int up_flag;

 	kbd = kbd_table + fg_console;
	up_flag = (keycode & 0x80);
        keycode &= 0x7f;

	if (!repeat)
		del_timer(&repeat_timer);

#ifdef CONFIG_ADBMOUSE
	/*
	 * XXX: Add mouse button 2+3 fake codes here if mouse open.
	 *	As we only report up/down events, keep track of faked buttons.
	 *	Really messy; might need to check if keyboard is in
	 *	VC_RAW mode for X?.
	 *	Might also want to know how many buttons need to be emulated.
	 *	-> hide this as function in arch/m68k/mac ?
	 *	Current emulation buttons: right alt/option and control
	 *	(wanted: command and alt/option, or KP= and KP( ...)
	 *	Debug version; might be rewritten to be faster on normal keys.
	 */
	if (adb_emulate_buttons 
	    && (adb_mouse_interrupt_hook || console_loglevel >= 8)) {
		unsigned char button, button2, button3, fake_event;
		static unsigned char button2state=0, button3state=0; /* up */
		/* faked ADB packet */
		static unsigned char data[4] = { 0, 0x80, 0x80, 0x80 };

		button = 0;
		fake_event = 0;
		if (keycode == adb_button2_keycode) {	/* which 'button' ? */
			/* R-option */
			button2 = (!up_flag);		/* new state */
			if (button2 != button2state)	/* change ? */
				button = 2; 
			button2state = button2;		/* save state */
			fake_event = 2;
		} else if (keycode == adb_button3_keycode) {
			/* R-control */
			button3 = (!up_flag);		/* new state */
			if (button3 != button3state)	/* change ? */ 
				button = 3; 
			button3state = button3; 	/* save state */
			fake_event = 3;
		}
#ifdef DEBUG_ADBMOUSE
		if (fake_event && console_loglevel >= 8)
			printk("fake event: button2 %d button3 %d button %d\n",
				 button2state, button3state, button);
#endif
		if (button) {		/* there's been a button state change */
			/* fake a mouse packet : send all bytes, change one! */
			data[button] = (up_flag ? 0x80 : 0);
			if (adb_mouse_interrupt_hook)
				adb_mouse_interrupt_hook(data, 4);
#ifdef DEBUG_ADBMOUSE
			else
				printk("mouse_fake: data %2x %2x %2x buttons %2x \n", 
					data[1], data[2], data[3],
					~( (data[1] & 0x80 ? 0 : 4) 
					 | (data[2] & 0x80 ? 0 : 1) 
					 | (data[3] & 0x80 ? 0 : 2) )&7 );
#endif
		}
		/*
		 * for mouse 3-button emulation: don't process 'fake' keys!
		 * Keys might autorepeat, and console state gets generally messed
		 * up enough so that selection stops working.
		 */
		if (fake_event)
			return;
	}
#endif /* CONFIG_ADBMOUSE */

	/*
	 * Convert R-shift/control/option to L version.
	 */
	switch (keycode) {
		case 0x7b: keycode = 0x38; break; /* R-shift */
		case 0x7c: keycode = 0x3a; break; /* R-option */
		case 0x7d: keycode = 0x36; break; /* R-control */
		case 0x0:  if (kbd->kbdmode != VC_RAW) 
			       keycode = 0x5a; /* A; keycode 0 deprecated */
			       break; 
	}

	if (kbd->kbdmode != VC_RAW) {
		if (!up_flag && !dont_repeat[keycode]) {
			last_keycode = keycode;
			repeat_timer.expires = jiffies + (repeat? HZ/15: HZ/2);
			add_timer(&repeat_timer);
		}

		/*
		 * XXX fix caps-lock behaviour by turning the key-up
		 * transition into a key-down transition.
		 * MSch: need to turn each caps-lock event into a down-up
		 * double event (keyboard code assumes caps-lock is a toggle)
		 * 981127: fix LED behavior (kudos atong!)
		 */
		switch (keycode) {
		case 0x39:
			handle_scancode(keycode);	/* down */
			up_flag = 0x80;			/* see below ... */
		 	mark_bh(KEYBOARD_BH);
			break;
		 case 0x47:
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
    Handler 4 -- Apple Extended mouse protocol.

    For Apple's 3-button mouse protocol the data array will contain the
    following values:

		BITS    COMMENTS
    data[0] = 0000 0000 ADB packet identifer.
    data[1] = 0100 0000 Extended protocol register.
	      Bits 6-7 are the device id, which should be 1.
	      Bits 4-5 are resolution which is in "units/inch".
	      The Logitech MouseMan returns these bits clear but it has
	      200/300cpi resolution.
	      Bits 0-3 are unique vendor id.
    data[2] = 0011 1100 Bits 0-1 should be zero for a mouse device.
	      Bits 2-3 should be 8 + 4.
		      Bits 4-7 should be 3 for a mouse device.
    data[3] = bxxx xxxx Left button and x-axis motion.
    data[4] = byyy yyyy Second button and y-axis motion.
    data[5] = byyy bxxx Third button and fourth button.  Y is additional
	      high bits of y-axis motion.  XY is additional
	      high bits of x-axis motion.

    NOTE: data[0] and data[2] are confirmed by the parent function and
    need not be checked here.
  */

  /*
    Handler 1 -- 100cpi original Apple mouse protocol.
    Handler 2 -- 200cpi original Apple mouse protocol.

    For Apple's standard one-button mouse protocol the data array will
    contain the following values:

                BITS    COMMENTS
    data[0] = 0000 0000 ADB packet identifer.
    data[1] = ???? ???? (?)
    data[2] = ???? ??00 Bits 0-1 should be zero for a mouse device.
    data[3] = bxxx xxxx First button and x-axis motion.
    data[4] = byyy yyyy Second button and y-axis motion.

    NOTE: data[0] is confirmed by the parent function and need not be
    checked here.
  */

static void
mouse_input(unsigned char *data, int nb, struct pt_regs *regs)
{
	struct kbd_struct *kbd;
	int i;

	if (nb < 5 || nb > 6 || (data[2] & 3) != MOUSE_DATAREG) {
		printk("data from mouse:");
		for (i = 0; i < nb; ++i)
			printk(" %x", data[i]);
		printk("\n");
		return;
	}

	if (adb_mouse_interrupt_hook) {
		adb_mouse_interrupt_hook(data+2, nb-2);
		/*
		 * passing the mouse data to i.e. the X server as done for
		 * Xpmac will confuse applications on a sane X server :-)
		 */
		return;
	} 
#ifdef DEBUG_ADBMOUSE
	else
		if (console_loglevel >= 8)
		    printk("mouse_input: data %x %x %x buttons %x dx %d dy %d \n", 
			data[3], data[4], data[5],
			~((data[3] & 0x80 ? 0 : 4) 
			| (data[4] & 0x80 ? 0 : 1)
			| (data[5] & 0x80 ? 0 : 2))&7,
			((data[4]&0x7f) < 64 ? (data[4]&0x7f) : (data[4]&0x7f)-128 ),
			((data[3]&0x7f) < 64 ? -(data[3]&0x7f) : 128-(data[3]&0x7f) ) );
#endif


	kbd = kbd_table + fg_console;

#if 0	/* The entirely insane way of MkLinux handling mouse input */
	/* Requires put_queue which is static in keyboard.c :-( */
	/* Only send mouse codes when keyboard is in raw mode. */
	if (kbd->kbdmode == VC_RAW) {
		static unsigned char uch_ButtonStateSecond = 0;
		unsigned char uchButtonSecond;

		/* Send first button, second button and movement. */
		put_queue( 0x7e );
		put_queue( data[3] );
		put_queue( data[4] );

		/* [ACA: Are there any two-button ADB mice that use handler 1 or 2?] */

		/* Store the button state. */
		uchButtonSecond = (data[4] & 0x80);

		/* Send second button. */
		if (uchButtonSecond != uch_ButtonStateSecond) {
			put_queue( 0x3f | uchButtonSecond );
			uch_ButtonStateSecond = uchButtonSecond;
		}

		/* Macintosh 3-button mouse (handler 4). */
		if ((nb == 6) && (data[1] & 0x40)) {
			static unsigned char uch_ButtonStateThird = 0;
			unsigned char uchButtonThird;

			/* Store the button state for speed. */
			uchButtonThird = (data[5] & 0x80);

			/* Send third button. */
			if (uchButtonThird != uch_ButtonStateThird) {
				put_queue( 0x40 | uchButtonThird );
				uch_ButtonStateThird = uchButtonThird;
			}
		}
	}
#endif	/* insane MkLinux mouse hack */
}

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

static int leds_pending;

void mac_kbd_leds(unsigned int leds)
{
	if (led_request.got_reply) {
#ifdef DEBUG_ADB
		if (console_loglevel == 10)
			printk("mac_kbd_leds: got reply, sending request!\n");
#endif
		adb_request(&led_request, mac_leds_done, 4, ADB_PACKET,
			     ADB_WRITEREG(ADB_KEYBOARD, KEYB_LEDREG),
			     0xff, ~mac_ledmap[leds]);
	} else
		leds_pending = leds | 0x100;
}

static void mac_leds_done(struct adb_request *req)
{
	int leds;

	if (leds_pending) {
		leds = leds_pending & 0xff;
		leds_pending = 0;
		mac_kbd_leds(leds);
	}
	mark_bh(KEYBOARD_BH);
}

int mac_kbdrate(struct kbd_repeat *k)
{
	return 0;
}

__initfunc(int mac_keyb_init(void))
{
	static struct adb_request autopoll_req, confcod_req, mouse_req, readkey_req;
	volatile int ct;

	/* setup key map */
	memcpy(key_maps[0], mac_plain_map, sizeof(plain_map));
	memcpy(key_maps[1], mac_shift_map, sizeof(plain_map));
	memcpy(key_maps[2], mac_altgr_map, sizeof(plain_map));
	memcpy(key_maps[4], mac_ctrl_map, sizeof(plain_map));
	memcpy(key_maps[5], mac_shift_ctrl_map, sizeof(plain_map));
	memcpy(key_maps[8], mac_alt_map, sizeof(plain_map));
	memcpy(key_maps[12], mac_ctrl_alt_map, sizeof(plain_map));

	/* initialize mouse interrupt hook */
	adb_mouse_interrupt_hook = NULL;

	/*
	 * Might put that someplace else, possibly ....
	 */
	adb_bus_init();

	/* the input functions ... */	
	adb_register(ADB_KEYBOARD, keyboard_input);
	adb_register(ADB_MOUSE, mouse_input);

	/* turn on ADB auto-polling in the CUDA */
	
	/*
	 *	Older boxes don't support CUDA_* targets and CUDA commands
	 *	instead we emulate them in the adb_request hook to make
	 *	the code interfaces saner.
	 *
	 *	Note XXX: the Linux PMac and this code both assume the
	 *	devices are at their primary ids and do not do device
	 *	assignment. This isn't ideal. We should fix it to follow
	 *	the reassignment specs.
	 */

	if (macintosh_config->adb_type == MAC_ADB_CUDA) {
		printk("CUDA autopoll on ...\n");
		adb_request(&autopoll_req, NULL, 3, CUDA_PACKET, CUDA_AUTOPOLL, 1);
		ct=0; 
		while (!autopoll_req.got_reply && ++ct<1000)
		{
				udelay(10);
		}
		if(ct==1000) {
			printk("Keyboard timed out.\n");
			autopoll_req.got_reply = 1;
		}
	}

	/*
	 *	XXX: all ADB requests now in CUDA format; adb_request takes 
	 *	care of that for other Macs.
	 */

	printk("Configuring keyboard:\n");

	udelay(3000);

	/* 
	 * turn on all leds - the keyboard driver will turn them back off 
	 * via mac_kbd_leds if everything works ok!
	 */
	printk("leds on ...\n");
	adb_request(&led_request, NULL, 4, ADB_PACKET,
		     ADB_WRITEREG(ADB_KEYBOARD, KEYB_LEDREG), 0xff, ~7);

	/*
	 * The polling stuff should go away as soon as the ADB driver is stable
	 */
	ct = 0;
	while (!led_request.got_reply && ++ct<1000)
	{
		udelay(10);
	}
	if(ct==1000) {
		printk("keyboard timed out.\n");
		led_request.got_reply  = 1;
	}

#if 1
	printk("configuring coding mode ...\n");

	udelay(3000);

	/* 
	 * get the keyboard to send separate codes for
	 * left and right shift, control, option keys. 
	 */
	adb_request(&confcod_req, NULL, 4, ADB_PACKET, 
		     ADB_WRITEREG(ADB_KEYBOARD, 3), 0, 3);

	ct=0; 
	while (!confcod_req.got_reply && ++ct<1000)
	{
		udelay(10);
	}
	if(ct==1000) {
		printk("keyboard timed out.\n");
		confcod_req.got_reply  = 1;
	}
#endif

#if 0	/* seems to hurt, at least Geert's Mac */
	printk("Configuring mouse (3-button mode) ...\n");

	udelay(3000);

	/* 
	 * XXX: taken from the PPC driver again ... 
	 * Try to switch the mouse (id 3) to handler 4, for three-button
	 * mode. (0x20 is Service Request Enable, 0x03 is Device ID). 
	 */
	adb_request(&mouse_req, NULL, 4, ADB_PACKET,
		    ADB_WRITEREG(ADB_MOUSE, 3), 0x23, 4 );

	ct=0; 
	while (!mouse_req.got_reply && ++ct<1000)
	{
		udelay(10);
	}
	if(ct==1000)
		printk("Mouse timed out.\n");
#endif

#if 0
	printk("Start polling keyboard ...\n");

	/* 
	 *	get the keyboard to send data back, via the adb_input hook
	 *	XXX: was never used properly, and the driver takes care
	 *	of polling and timeout retransmits now.
	 *	Might be of use if we want to start talking to a specific
	 *	device here...
	 */
	adb_request(&readkey_req, NULL, 2, ADB_PACKET, 
		     ADB_READREG(ADB_KEYBOARD, KEYB_KEYREG));
#endif

	in_keybinit = 0;
	printk("keyboard init done\n");

	return 0;
}
