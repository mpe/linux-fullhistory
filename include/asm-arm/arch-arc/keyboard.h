/*
 * linux/include/asm-arm/arch-arc/keyboard.h
 *
 * Keyboard driver definitions for Acorn Archimedes/A5000
 * architecture
 *
 * Copyright (C) 1998 Russell King
 */

#include <asm/irq.h>

#define NR_SCANCODES 128

extern int a5kkbd_translate(unsigned char scancode, unsigned char *keycode_p, char *up_flag_p);
extern void a5kkbd_leds(unsigned char leds);
extern void a5kkbd_init_hw(void);
extern unsigned char a5kkbd_sysrq_xlate[NR_SCANCODES];

#define kbd_setkeycode(sc,kc)		(-EINVAL)
#define kbd_getkeycode(sc)		(-EINVAL)

/* Prototype: int kbd_pretranslate(scancode, raw_mode)
 * Returns  : 0 to ignore scancode
 */
#define kbd_pretranslate(sc,rm)	(1)

/* Prototype: int kbd_translate(scancode, *keycode, *up_flag, raw_mode)
 * Returns  : 0 to ignore scancode, *keycode set to keycode, *up_flag
 *            set to 0200 if scancode indicates release
 */
#define kbd_translate(sc, kcp, ufp, rm)	a5kkbd_translate(sc, kcp, ufp)
#define kbd_unexpected_up(kc)		(0200)
#define kbd_leds(leds)			a5kkbd_leds(leds)
#define kbd_init_hw()			a5kkbd_init_hw()
#define kbd_sysrq_xlate			a5kkbd_sysrq_xlate
#define kbd_disable_irq()		disable_irq(IRQ_KEYBOARDRX)
#define kbd_enable_irq()		enable_irq(IRQ_KEYBOARDRX)

#define SYSRQ_KEY	13
