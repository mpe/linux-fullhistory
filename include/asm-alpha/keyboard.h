/*
 *  linux/include/asm-alpha/keyboard.h
 *
 *  Created 3 Nov 1996 by Geert Uytterhoeven
 */

/*
 *  This file contains the alpha architecture specific keyboard definitions
 */

#ifndef _ALPHA_KEYBOARD_H
#define _ALPHA_KEYBOARD_H

#ifdef __KERNEL__

#define KEYBOARD_IRQ			1
#define DISABLE_KBD_DURING_INTERRUPTS	0

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_pretranslate(unsigned char scancode, char raw_mode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);
extern unsigned char pckbd_sysrq_xlate[128];

#define kbd_setkeycode		pckbd_setkeycode
#define kbd_getkeycode		pckbd_getkeycode
#define kbd_pretranslate	pckbd_pretranslate
#define kbd_translate		pckbd_translate
#define kbd_unexpected_up	pckbd_unexpected_up
#define kbd_leds		pckbd_leds
#define kbd_init_hw		pckbd_init_hw
#define kbd_sysrq_xlate		pckbd_sysrq_xlate

#define INIT_KBD

#define SYSRQ_KEY 0x54

#endif /* __KERNEL__ */

#endif /* __ASMalpha_KEYBOARD_H */
