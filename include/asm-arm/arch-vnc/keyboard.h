/*
 * linux/include/asm-arm/arch-ebsa285/keyboard.h
 *
 * Keyboard driver definitions for EBSA285 architecture
 *
 * (C) 1998 Russell King
 */

#include <linux/config.h>
#include <asm/irq.h>

#define NR_SCANCODES 128

#ifdef CONFIG_MAGIC_SYSRQ
static unsigned char kbd_sysrq_xlate[NR_SCANCODES];
#endif

#define kbd_setkeycode(sc,kc)		(-EINVAL)
#define kbd_getkeycode(sc)		(-EINVAL)

/* Prototype: int kbd_pretranslate(scancode, raw_mode)
 * Returns  : 0 to ignore scancode
 */
#define kbd_pretranslate(sc,rm)		(1)

/* Prototype: int kbd_translate(scancode, *keycode, *up_flag, raw_mode)
 * Returns  : 0 to ignore scancode, *keycode set to keycode, *up_flag
 *            set to 0200 if scancode indicates release
 */
#define kbd_translate(sc, kcp, ufp, rm)	(1)
#define kbd_unexpected_up(kc)		(0200)
#define kbd_leds(leds)
#define kbd_init_hw()
//#define kbd_sysrq_xlate			ps2kbd_sysrq_xlate
#define kbd_disable_irq()
#define kbd_enable_irq()
