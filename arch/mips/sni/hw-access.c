/* $Id: hw-access.c,v 1.8 1998/09/16 22:50:46 ralf Exp $
 *
 * Low-level hardware access stuff for SNI RM200 PCI
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/kbdcntrlr.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/keyboard.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mc146818rtc.h>
#include <asm/pgtable.h>
#include <asm/sni.h>

#define KBD_STAT_IBF		0x02	/* Keyboard input buffer full */

static unsigned char sni_read_input(void)
{
	return inb(KBD_DATA_REG);
}

static void sni_write_output(unsigned char val)
{
	int status;

	do {
		status = inb(KBD_CNTL_REG);
	} while (status & KBD_STAT_IBF);
	outb(val, KBD_DATA_REG);
}

static void sni_write_command(unsigned char val)
{
	int status;

	do {
		status = inb(KBD_CNTL_REG);
	} while (status & KBD_STAT_IBF);
	outb(val, KBD_CNTL_REG);
}

static unsigned char sni_read_status(void)
{
	return inb(KBD_STATUS_REG);
}

__initfunc(void sni_rm200_keyboard_setup(void))
{
	kbd_read_input = sni_read_input;
	kbd_write_output = sni_write_output;
	kbd_write_command = sni_write_command;
	kbd_read_status = sni_read_status;
	request_irq(PCIMT_KEYBOARD_IRQ, keyboard_interrupt,
	            0, "keyboard", NULL);
	request_region(0x60, 16, "keyboard");
}
