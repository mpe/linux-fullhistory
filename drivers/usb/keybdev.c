/*
 *  keybdev.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Input driver to keyboard driver binding.
 *
 *  Sponsored by SuSE
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/config.h>
#include <linux/kbd_ll.h>
#include <linux/input.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kbd_kern.h>

#if defined(CONFIG_X86) || defined(CONFIG_IA64)

static unsigned char keybdev_x86_e0s[] = 
	{ 0x1c, 0x1d, 0x35, 0x2a, 0x38, 0x39, 0x47, 0x48,
	  0x49, 0x4b, 0x4d, 0x4f, 0x50, 0x51, 0x52, 0x53,
	  0x26, 0x25, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x00,
	  0x23, 0x24, 0x25, 0x26, 0x27 };

#elif CONFIG_ADB_KEYBOARD

static unsigned char keybdev_mac_codes[256] =
	{ 0, 53, 18, 19, 20, 21, 23, 22, 26, 28, 25, 29, 27, 24, 51, 48,
	 12, 13, 14, 15, 17, 16, 32, 34, 31, 35, 33, 30, 36, 54,128,  1,
	  2,  3,  5,  4, 38, 40, 37, 41, 39, 50, 56, 42,  6,  7,  8,  9,
	 11, 45, 46, 43, 47, 44,123, 67, 58, 49, 57,122,120, 99,118, 96,
	 97, 98,100,101,109, 71,107, 89, 91, 92, 78, 86, 87, 88, 69, 83,
	 84, 85, 82, 65, 42,  0, 10,103,111,  0,  0,  0,  0,  0,  0,  0,
	 76,125, 75,105,124,  0,115, 62,116, 59, 60,119, 61,121,114,117,
	  0,  0,  0,  0,127, 81,  0,113,  0,  0,  0,  0,  0, 55, 55 };

#endif

struct input_handler keybdev_handler;

void keybdev_ledfunc(unsigned int led)
{
	struct input_handle *handle;	

	for (handle = keybdev_handler.handle; handle; handle = handle->hnext) {

		input_event(handle->dev, EV_LED, LED_SCROLLL, !!(led & 0x01));
		input_event(handle->dev, EV_LED, LED_NUML,    !!(led & 0x02));
		input_event(handle->dev, EV_LED, LED_CAPSL,   !!(led & 0x04));

	}
}

void keybdev_event(struct input_handle *handle, unsigned int type, unsigned int code, int down)
{
	if (type != EV_KEY || code > 255) return;

#if defined(CONFIG_X86) || defined(CONFIG_IA64)

	if (code >= 189) {
  		printk(KERN_WARNING "keybdev.c: can't emulate keycode %d\n", code);
		return; 
	} else if (code >= 162) {
		handle_scancode(0xe0, 1);
		handle_scancode(code - 161, down);
	} else if (code >= 125) {
		handle_scancode(0xe0, 1);
		handle_scancode(code - 34, down);
	} else if (code == 119) {
		handle_scancode(0xe1, 1);
		handle_scancode(0x1d, down);
		handle_scancode(0x45, down);
	} else if (code >= 96) {
		handle_scancode(0xe0, 1);
		handle_scancode(keybdev_x86_e0s[code - 96], down);
		if (code == 99) {
			handle_scancode(0xe0, 1);
			handle_scancode(0x37, down);
		}
	} else handle_scancode(code, down);

#elif CONFIG_ADB_KEYBOARD

	if (code < 128 && keybdev_mac_codes[code]) 
		handle_scancode(keybdev_mac_codes[code] & 0x7f, down);
	else
		printk(KERN_WARNING "keybdev.c: can't emulate keycode %d\n", code);

#else
#error "Cannot generate rawmode keyboard for your architecture yet."
#endif

	tasklet_schedule(&keyboard_tasklet);
}

static int keybdev_connect(struct input_handler *handler, struct input_dev *dev)
{
	struct input_handle *handle;
	int i;

	if (!test_bit(EV_KEY, dev->evbit))
		return -1;

	for (i = KEY_RESERVED; i < BTN_MISC; i++)
		if (test_bit(i, dev->keybit)) break;

	if (i == BTN_MISC)
 		return -1;

	if (!(handle = kmalloc(sizeof(struct input_handle), GFP_KERNEL)))
		return -1;
	memset(handle, 0, sizeof(struct input_handle));

	handle->dev = dev;
	handle->handler = handler;

	input_open_device(handle);

	printk("keybdev.c: Adding keyboard: input%d\n", dev->number);

	return 0;
}

static void keybdev_disconnect(struct input_handle *handle)
{
	printk("keybdev.c: Removing keyboard: input%d\n", handle->dev->number);

	input_close_device(handle);

	kfree(handle);
}
	
struct input_handler keybdev_handler = {
	event:		keybdev_event,
	connect:	keybdev_connect,
	disconnect:	keybdev_disconnect,
};

#ifdef MODULE
void cleanup_module(void)
{
	kbd_ledfunc = NULL;
	input_unregister_handler(&keybdev_handler);
}
int init_module(void)
#else
int __init keybdev_init(void)
#endif
{
	input_register_handler(&keybdev_handler);
	kbd_ledfunc = keybdev_ledfunc;
	return 0;
}
