/*
 * linux/include/asm-arm/arch-ebsa285/keyboard.h
 *
 * Keyboard driver definitions for EBSA285 architecture
 *
 * (C) 1998 Russell King
 * (C) 1998 Phil Blundell
 */

#include <linux/config.h>
#include <asm/irq.h>
#include <asm/system.h>

#define NR_SCANCODES 128

#ifdef CONFIG_CATS

#define KEYBOARD_IRQ		IRQ_ISA(1)

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
#define kbd_init_hw()			\
		do { if (machine_is_cats()) pckbd_init_hw(); } while (0)
#define kbd_sysrq_xlate			pckbd_sysrq_xlate
#define kbd_disable_irq()
#define kbd_enable_irq()

#define SYSRQ_KEY	0x54

#else

/* Dummy keyboard definitions */

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

#define SYSRQ_KEY	13

#endif
