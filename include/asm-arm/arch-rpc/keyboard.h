/*
 * linux/include/asm-arm/arch-rpc/keyboard.h
 *
 * Keyboard driver definitions for RiscPC architecture
 *
 * (C) 1998 Russell King
 */

#include <asm/irq.h>

#define NR_SCANCODES 128

extern int ps2kbd_pretranslate(unsigned char scancode);
extern int ps2kbd_translate(unsigned char scancode, unsigned char *keycode_p, char *up_flag_p);
extern void ps2kbd_leds(unsigned char leds);
extern void ps2kbd_init_hw(void);
extern unsigned char ps2kbd_sysrq_xlate[NR_SCANCODES];

#define kbd_setkeycode(sc,kc)		(-EINVAL)
#define kbd_getkeycode(sc)		(-EINVAL)

/* Prototype: int kbd_pretranslate(scancode, raw_mode)
 * Returns  : 0 to ignore scancode
 */
#define kbd_pretranslate(sc,rm)		ps2kbd_pretranslate(sc)

/* Prototype: int kbd_translate(scancode, *keycode, *up_flag, raw_mode)
 * Returns  : 0 to ignore scancode, *keycode set to keycode, *up_flag
 *            set to 0200 if scancode indicates release
 */
#ifdef NEW_KEYBOARD
#define kbd_translate(sc, kcp, ufp, rm)	ps2kbd_translate(sc, kcp, ufp)
#else
#define kbd_translate(sc, kcp, rm) ({ unsigned int up_flag; ps2kbd_translate(sc, kcp, &up_flag); })
#endif
#define kbd_unexpected_up(kc)		(0200)
#define kbd_leds(leds)			ps2kbd_leds(leds)
#define kbd_init_hw()			ps2kbd_init_hw()
#define kbd_sysrq_xlate			ps2kbd_sysrq_xlate
#define kbd_disable_irq()		disable_irq(IRQ_KEYBOARDRX)
#define kbd_enable_irq()		enable_irq(IRQ_KEYBOARDRX)

#define SYSRQ_KEY	13
