/*
 *  linux/include/asm-m68k/keyboard.h
 *
 *  Created 3 Nov 1996 by Geert Uytterhoeven
 */

/*
 *  This file contains the m68k architecture specific keyboard definitions
 */

#include <linux/config.h> /* CONFIG_MAGIC_SYSRQ */
#ifndef __M68K_KEYBOARD_H
#define __M68K_KEYBOARD_H

#ifdef __KERNEL__

#include <asm/machdep.h>

static __inline__ int kbd_setkeycode(unsigned int scancode,
				     unsigned int keycode)
{
    return -EOPNOTSUPP;
}

static __inline__ int kbd_getkeycode(unsigned int scancode)
{
    return scancode > 127 ? -EINVAL : scancode;
}

static __inline__ int kbd_pretranslate(unsigned char scancode, char raw_mode)
{
    return 1;
}

static __inline__ int kbd_translate(unsigned char scancode,
				    unsigned char *keycode, char raw_mode)
{
    *keycode = scancode;
    return 1;
}

static __inline__ char kbd_unexpected_up(unsigned char keycode)
{
    return 0200;
}

static __inline__ void kbd_leds(unsigned char leds)
{
    if (mach_kbd_leds)
	mach_kbd_leds(leds);
}

#ifdef CONFIG_MAGIC_SYSRQ
#define kbd_is_sysrq(keycode)	((keycode) == mach_sysrq_key && \
				 (up_flag || \
				  (shift_state & mach_sysrq_shift_mask) == \
				  mach_sysrq_shift_state))
#define kbd_sysrq_xlate			mach_sysrq_xlate
#endif

#define kbd_init_hw	mach_keyb_init

#endif /* __KERNEL__ */

#endif /* __M68K_KEYBOARD_H */
