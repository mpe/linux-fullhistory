/*
 * linux/include/asm-arm/arch-ebsa285/keyboard.h
 *
 * Keyboard driver definitions for EBSA285 architecture
 *
 * (C) 1998 Russell King
 * (C) 1998 Phil Blundell
 */
#include <asm/irq.h>
#include <asm/system.h>

extern int have_isa_bridge;

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);
extern unsigned char pckbd_sysrq_xlate[128];

#define KEYBOARD_IRQ			IRQ_ISA_KEYBOARD

#define NR_SCANCODES 128

#define kbd_setkeycode(sc,kc)				\
	({						\
		int __ret;				\
		if (have_isa_bridge)			\
			__ret = pckbd_setkeycode(sc,kc);\
		else					\
			__ret = -EINVAL;		\
		__ret;					\
	})

#define kbd_getkeycode(sc)				\
	({						\
		int __ret;				\
		if (have_isa_bridge)			\
			__ret = pckbd_getkeycode(sc);	\
		else					\
			__ret = -EINVAL;		\
		__ret;					\
	})

#define kbd_translate(sc, kcp, rm)			\
	({						\
		pckbd_translate(sc, kcp, rm);		\
	})

#define kbd_unexpected_up		pckbd_unexpected_up

#define kbd_leds(leds)					\
	do {						\
		if (have_isa_bridge)			\
			pckbd_leds(leds);		\
	} while (0)

#define kbd_init_hw()					\
	do {						\
		if (have_isa_bridge)			\
			pckbd_init_hw();		\
	} while (0)

#define kbd_sysrq_xlate			pckbd_sysrq_xlate

#define kbd_disable_irq()
#define kbd_enable_irq()

#define SYSRQ_KEY	0x54
