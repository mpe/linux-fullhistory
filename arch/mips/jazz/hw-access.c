/* $Id: hw-access.c,v 1.11 1998/09/16 22:50:39 ralf Exp $
 *
 * Low-level hardware access stuff for Jazz family machines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 1998 by Ralf Baechle
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <asm/addrspace.h>
#include <asm/jazz.h>
#include <asm/jazzdma.h>
#include <asm/keyboard.h>
#include <asm/pgtable.h>

static volatile keyboard_hardware *jazz_kh = 
	(keyboard_hardware *) JAZZ_KEYBOARD_ADDRESS;

#define KBD_STAT_IBF		0x02	/* Keyboard input buffer full */

static unsigned char jazz_read_input(void)
{
	return jazz_kh->data;
}

static void jazz_write_output(unsigned char val)
{
	int status;

	do {
		status = jazz_kh->command;
	} while (status & KBD_STAT_IBF);
	jazz_kh->data = val;
}

static void jazz_write_command(unsigned char val)
{
	int status;

	do {
		status = jazz_kh->command;
	} while (status & KBD_STAT_IBF);
	jazz_kh->command = val;
}

static unsigned char jazz_read_status(void)
{
	return jazz_kh->command;
}

__initfunc(void jazz_keyboard_setup(void))
{
	kbd_read_input = jazz_read_input;
	kbd_write_output = jazz_write_output;
	kbd_write_command = jazz_write_command;
	kbd_read_status = jazz_read_status;
	request_irq(JAZZ_KEYBOARD_IRQ, keyboard_interrupt,
	            0, "keyboard", NULL);
	request_region(0x60, 16, "keyboard");
	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE,
	                  r4030_read_reg16(JAZZ_IO_IRQ_ENABLE)
	                  | JAZZ_IE_KEYBOARD);
}

int jazz_ps2_request_irq(void)
{
    extern void aux_interrupt(int, void *, struct pt_regs *);
    int ret;
    
    ret = request_irq(JAZZ_MOUSE_IRQ, aux_interrupt, 0, "PS/2 Mouse", NULL);
    if (!ret)
	r4030_write_reg16(JAZZ_IO_IRQ_ENABLE, 
			  r4030_read_reg16(JAZZ_IO_IRQ_ENABLE) | 
			  JAZZ_IE_MOUSE);
    return ret;
}

void jazz_ps2_free_irq(void)
{
    r4030_write_reg16(JAZZ_IO_IRQ_ENABLE, 
		      r4030_read_reg16(JAZZ_IO_IRQ_ENABLE) | 
		      JAZZ_IE_MOUSE);
    free_irq(JAZZ_MOUSE_IRQ, NULL);
}
