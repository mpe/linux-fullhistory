/*
 * CPU specific parts of the keyboard driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file is a mess.  Put on your peril sensitive glasses.
 *
 * $Id: keyboard.h,v 1.4 1997/06/16 00:31:46 ralf Exp $
 */
#ifndef __ASM_MIPS_KEYBOARD_H
#define __ASM_MIPS_KEYBOARD_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/delay.h>
#include <linux/ioport.h>

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_pretranslate(unsigned char scancode, char raw_mode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);

#define kbd_setkeycode		pckbd_setkeycode
#define kbd_getkeycode		pckbd_getkeycode
#define kbd_pretranslate	pckbd_pretranslate
#define kbd_translate		pckbd_translate
#define kbd_unexpected_up	pckbd_unexpected_up
#define kbd_leds		pckbd_leds
#define kbd_init_hw		pckbd_init_hw

/*
 * The default IO slowdown is doing 'inb()'s from 0x61, which should be
 * safe. But as that is the keyboard controller chip address, we do our
 * slowdowns here by doing short jumps: the keyboard controller should
 * be able to keep up
 */
#define REALLY_SLOW_IO
#define SLOW_IO_BY_JUMPING
#include <asm/io.h>

/*
 * keyboard controller registers
 */
#define KBD_STATUS_REG      (unsigned int) 0x64
#define KBD_CNTL_REG        (unsigned int) 0x64
#define KBD_DATA_REG        (unsigned int) 0x60

#ifdef CONFIG_SGI
#include <asm/segment.h>
#include <asm/sgihpc.h>
#endif
#include <asm/bootinfo.h>
#include <asm/jazz.h>

#ifdef CONFIG_SGI
#define KEYBOARD_IRQ 20
#else
/* Not true for Jazz machines, we cheat a bit for 'em. */
#define KEYBOARD_IRQ 1
#endif

#ifdef CONFIG_SGI
#define DISABLE_KBD_DURING_INTERRUPTS 1
#else
#define DISABLE_KBD_DURING_INTERRUPTS 0
#endif

#ifndef CONFIG_SGI
#define KBD_REPORT_ERR
#endif
#define KBD_REPORT_UNKN
/* #define KBD_IS_FOCUS_9000 */

int (*kbd_inb_p)(unsigned short port);
int (*kbd_inb)(unsigned short port);
void (*kbd_outb_p)(unsigned char data, unsigned short port);
void (*kbd_outb)(unsigned char data, unsigned short port);

#ifdef CONFIG_MIPS_JAZZ
#define INIT_KBD	/* full initialization for the keyboard controller. */

static volatile keyboard_hardware *jazz_kh;

static int
jazz_kbd_inb_p(unsigned short port)
{
	int result;

	if(port == KBD_DATA_REG)
		result = jazz_kh->data;
	else /* Must be KBD_STATUS_REG */
		result = jazz_kh->command;
	inb(0x80);

	return result;
}

static int
jazz_kbd_inb(unsigned short port)
{
	int result;

	if(port == KBD_DATA_REG)
		result = jazz_kh->data;
	else /* Must be KBD_STATUS_REG */
		result = jazz_kh->command;

	return result;
}

static void
jazz_kbd_outb_p(unsigned char data, unsigned short port)
{
	if(port == KBD_DATA_REG)
		jazz_kh->data = data;
	else if(port == KBD_CNTL_REG)
		jazz_kh->command = data;
	inb(0x80);
}

static void
jazz_kbd_outb(unsigned char data, unsigned short port)
{
	if(port == KBD_DATA_REG)
		jazz_kh->data = data;
	else if(port == KBD_CNTL_REG)
		jazz_kh->command = data;
}
#endif /* CONFIG_MIPS_JAZZ */

#ifdef CONFIG_SGI
#define INIT_KBD	/* full initialization for the keyboard controller. */

static volatile struct hpc_keyb *sgi_kh;

static int
sgi_kbd_inb(unsigned short port)
{
	int result;

	if(port == KBD_DATA_REG)
		result = sgi_kh->data;
	else /* Must be KBD_STATUS_REG */
		result = sgi_kh->command;

	return result;
}

static void
sgi_kbd_outb(unsigned char data, unsigned short port)
{
	if(port == KBD_DATA_REG)
		sgi_kh->data = data;
	else if(port == KBD_CNTL_REG)
		sgi_kh->command = data;
}
#endif /* CONFIG_SGI */

/*
 * Most other MIPS machines access the keyboard controller via
 * ordinary I/O ports.
 */
static int
port_kbd_inb_p(unsigned short port)
{
	return inb_p(port);
}

static int
port_kbd_inb(unsigned short port)
{
	return inb(port);
}

static void
port_kbd_outb_p(unsigned char data, unsigned short port)
{
	return outb_p(data, port);
}

static void
port_kbd_outb(unsigned char data, unsigned short port)
{
	return outb(data, port);
}

extern __inline__ void keyboard_setup(void)
{
#ifdef CONFIG_MIPS_JAZZ
        if (mips_machgroup == MACH_GROUP_JAZZ) {
		jazz_kh = (void *) JAZZ_KEYBOARD_ADDRESS;
		kbd_inb_p = jazz_kbd_inb_p;
		kbd_inb = jazz_kbd_inb;
		kbd_outb_p = jazz_kbd_outb_p;
		kbd_outb = jazz_kbd_outb;
		/*
		 * Enable keyboard interrupts.
		 */
		*((volatile u16 *)JAZZ_IO_IRQ_ENABLE) |= JAZZ_IE_KEYBOARD;
		set_cp0_status(IE_IRQ1, IE_IRQ1);
	} else
#endif
	if (mips_machgroup == MACH_GROUP_ARC ||	/* this is for Deskstation */
	    (mips_machgroup == MACH_GROUP_SNI_RM
	     && mips_machtype == MACH_SNI_RM200_PCI)) {
		/*
		 * These machines address their keyboard via the normal
		 * port address range.
		 *
		 * Also enable Scan Mode 2.
		 */
		kbd_inb_p = port_kbd_inb_p;
		kbd_inb = port_kbd_inb;
		kbd_outb_p = port_kbd_outb_p;
		kbd_outb = port_kbd_outb;
		request_region(0x60,16,"keyboard");
	}
#ifdef CONFIG_SGI
	if (mips_machgroup == MACH_GROUP_SGI) {
		sgi_kh = (struct hpc_keyb *) (KSEG1 + 0x1fbd9800 + 64);
		kbd_inb_p = sgi_kbd_inb;
		kbd_inb = sgi_kbd_inb;
		kbd_outb_p = sgi_kbd_outb;
		kbd_outb = sgi_kbd_outb;
	}
#endif
}

#endif /* __KERNEL */
#endif /* __ASM_MIPS_KEYBOARD_H */
