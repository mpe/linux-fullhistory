/*
 * linux/include/asm-arm/arch-sa1100/keyboard.h
 *
 * Keyboard driver definitions for SA1100 architecture
 *
 * This really has to be cleaned up somehow...
 * 
 */

#define KEYBOARD_IRQ 

#define NR_SCANCODES 128

#define kbd_setkeycode(sc,kc)  (-EINVAL)
#define kbd_getkeycode(sc)     (-EINVAL)
#define kbd_pretranslate(sc,kc)       1
#define kbd_translate(sc, kcp, raw)   kbd_drv_translate(sc, kcp, raw)
#define kbd_init_hw()	kbd_drv_init()
#define kbd_unexpected_up		

#define kbd_leds(leds)	

#define kbd_sysrq_xlate			
#define kbd_disable_irq()
#define kbd_enable_irq()

#define SYSRQ_KEY	0x54
