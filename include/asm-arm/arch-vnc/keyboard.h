/*
 * linux/include/asm-arm/arch-vnc/keyboard.h
 *
 * Keyboard driver definitions for VNC architecture
 *
 * (C) 1998 Russell King
 */

#include <asm/irq.h>

#define NR_SCANCODES 128

#define KEYBOARD_IRQ			IRQ_KEYBOARD

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_pretranslate(unsigned char scancode, char raw_mode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);
extern unsigned char pckbd_sysrq_xlate[128];

#define kbd_setkeycode			pckbd_setkeycode
#define kbd_getkeycode			pckbd_getkeycode
#define kbd_pretranslate		pckbd_pretranslate
#define kbd_translate(sc, kcp, ufp, rm) ({ *ufp = sc & 0200; \
		pckbd_translate(sc & 0x7f, kcp, rm);})

#define kbd_unexpected_up		pckbd_unexpected_up
#define kbd_leds			pckbd_leds
#define kbd_init_hw()			pckbd_init_hw()
#define kbd_sysrq_xlate			pckbd_sysrq_xlate
#define kbd_disable_irq()
#define kbd_enable_irq()

#define SYSRQ_KEY 0x54
