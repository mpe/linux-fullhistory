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

#include <asm/keyboard.h>
#include <asm/bitops.h>
#include <asm/cuda.h>

#include <linux/kbd_kern.h>
#include <linux/kbd_ll.h>

#define KEYB_KEYREG	0	/* register # for key up/down data */
#define KEYB_LEDREG	2	/* register # for leds on ADB keyboard */
#define MOUSE_DATAREG	0	/* reg# for movement/button codes from mouse */

unsigned char kbd_read_mask = 0;	/* XXX */

static void kbd_repeat(unsigned long);
static struct timer_list repeat_timer = { NULL, NULL, 0, 0, kbd_repeat };
static int last_keycode;

static void keyboard_input(unsigned char *, int, struct pt_regs *);
static void input_keycode(int, int);
static void leds_done(struct cuda_request *);

extern struct kbd_struct kbd_table[];

extern void handle_scancode(unsigned char);
extern void put_queue(int);

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

int mackbd_unexpected_up(unsigned char keycode)
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

	if (kbd->kbdmode != VC_RAW) {
		if (!up_flag && !dont_repeat[keycode]) {
			last_keycode = keycode;
			repeat_timer.expires = jiffies + (repeat? HZ/15: HZ/2);
			add_timer(&repeat_timer);
		}

		/*
		 * XXX fix caps-lock behaviour by turning the key-up
		 * transition into a key-down transition.
		 */
		if (keycode == 0x39 && up_flag && vc_kbd_led(kbd, VC_CAPSLOCK))
			up_flag = 0;
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

static void
mouse_input(unsigned char *data, int nb, struct pt_regs *regs)
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
	struct kbd_struct *kbd;

	kbd = kbd_table + fg_console;

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

static struct cuda_request led_request;
static int leds_pending;

void mackbd_leds(unsigned char leds)
{
	if (led_request.got_reply) {
		cuda_request(&led_request, leds_done, 4, ADB_PACKET,
			     ADB_WRITEREG(ADB_KEYBOARD, KEYB_LEDREG),
			     0xff, ~mac_ledmap[leds]);
	} else
		leds_pending = leds | 0x100;
}

static void leds_done(struct cuda_request *req)
{
	int leds;

	if (leds_pending) {
		leds = leds_pending & 0xff;
		leds_pending = 0;
		mackbd_leds(leds);
	}
}

void mackbd_init_hw(void)
{
	struct cuda_request req;

	adb_register(ADB_KEYBOARD, keyboard_input);
	adb_register(ADB_MOUSE, mouse_input);

	/* turn on ADB auto-polling in the CUDA */
	cuda_request(&req, NULL, 3, CUDA_PACKET, CUDA_AUTOPOLL, 1);
	while (!req.got_reply)
	    cuda_poll();

	/* turn off all leds */
	cuda_request(&req, NULL, 4, ADB_PACKET,
		     ADB_WRITEREG(ADB_KEYBOARD, KEYB_LEDREG), 0xff, 0xff);
	while (!req.got_reply)
	    cuda_poll();

	/* get the keyboard to send separate codes for
	   left and right shift, control, option keys. */
	cuda_request(&req, NULL, 4, ADB_PACKET,
		     ADB_WRITEREG(ADB_KEYBOARD, 3), 0, 3);
	while (!req.got_reply)
	    cuda_poll();

	led_request.got_reply = 1;

	/* Try to switch the mouse (id 3) to handler 4, for three-button
	   mode. (0x20 is Service Request Enable, 0x03 is Device ID). */
	cuda_request(&req, NULL, 4, ADB_PACKET,
		     ADB_WRITEREG(ADB_MOUSE, 3), 0x23, 4 );
	while (!req.got_reply)
		cuda_poll();
}

