/*
 *  linux/include/asm-ppc/keyboard.h
 *
 *  Created 3 Nov 1996 by Geert Uytterhoeven
 *  Modified for Power Macintosh by Paul Mackerras
 */

/*
 * This file contains the ppc architecture specific keyboard definitions -
 * like the intel pc for prep systems, different for power macs.
 */

#ifndef __ASMPPC_KEYBOARD_H
#define __ASMPPC_KEYBOARD_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/adb.h>
#include <asm/machdep.h>
#ifdef CONFIG_APUS
#include <asm-m68k/keyboard.h>
#else

#define KEYBOARD_IRQ			1
#define DISABLE_KBD_DURING_INTERRUPTS	0
#define INIT_KBD

static inline int kbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	return ppc_md.kbd_setkeycode(scancode, keycode);
}
  
static inline int kbd_getkeycode(unsigned int scancode)
{
	return ppc_md.kbd_getkeycode(scancode);
}
  
static inline int kbd_translate(unsigned char keycode, unsigned char *keycodep,
				char raw_mode)
{
	return ppc_md.kbd_translate(keycode, keycodep, raw_mode);
}
  
static inline int kbd_unexpected_up(unsigned char keycode)
{
	return ppc_md.kbd_unexpected_up(keycode);
}
  
static inline void kbd_leds(unsigned char leds)
{
	ppc_md.kbd_leds(leds);
}
  
static inline void kbd_init_hw(void)
{
	ppc_md.kbd_init_hw();
}

#define kbd_sysrq_xlate	(ppc_md.kbd_sysrq_xlate)

#ifdef CONFIG_MAC_KEYBOARD
# define SYSRQ_KEY 0x69
#else
# define SYSRQ_KEY 0x54
#endif

#endif /* CONFIG_APUS */

#endif /* __KERNEL__ */

#endif /* __ASMPPC_KEYBOARD_H */
