/*
 * CPU specific parts of the keyboard driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * $Id: keyboard.h,v 1.12 1998/09/16 22:52:41 ralf Exp $
 */
#ifndef __ASM_MIPS_KEYBOARD_H
#define __ASM_MIPS_KEYBOARD_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <asm/bootinfo.h>

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
#define kbd_sysrq_xlate         pckbd_sysrq_xlate

#define SYSRQ_KEY 0x54

/* Some stoneage hardware needs delays after some operations.  */
#define kbd_pause() do { } while(0)

/* Pointers to keyboard hardware access and init functions.  */
unsigned char (*kbd_read_input)(void);
void (*kbd_write_output)(unsigned char val);
void (*kbd_write_command)(unsigned char val);
unsigned char (*kbd_read_status)(void);

void (*keyboard_setup)(void);

#ifdef CONFIG_MIPS_JAZZ

extern int jazz_ps2_request_irq(void);
extern void jazz_ps2_free_irq(void);

#define ps2_request_irq()      jazz_ps2_request_irq()
#define ps2_free_irq(inode)    jazz_ps2_free_irq()

#endif /* CONFIG_MIPS_JAZZ */

#ifdef CONFIG_SGI

#define DISABLE_KBD_DURING_INTERRUPTS 1

/*
 * Machine specific bits for the PS/2 driver.
 * Aux device and keyboard share the interrupt on the Indy.
 */
#define ps2_request_irq() 0
#define ps2_free_irq(void) do { } while(0);

#endif /* CONFIG_SGI */

#if defined(CONFIG_ACER_PICA_61) || defined(CONFIG_SNI_RM200_PCI)
#define CONF_KEYBOARD_USES_IO_PORTS
#endif

#ifdef CONF_KEYBOARD_USES_IO_PORTS
/*
 * Most other MIPS machines access the keyboard controller via
 * memory mapped I/O ports.
 */
#include <asm/io.h>

/*
 * Machine specific bits for the PS/2 driver
 */

#define AUX_IRQ 12

#define ps2_request_irq()						\
	request_irq(AUX_IRQ, aux_interrupt, 0, "PS/2 Mouse", NULL)

#define ps2_free_irq(inode) free_irq(AUX_IRQ, NULL)

#endif /* CONF_KEYBOARD_USES_IO_PORTS */

#endif /* __KERNEL */
#endif /* __ASM_MIPS_KEYBOARD_H */
