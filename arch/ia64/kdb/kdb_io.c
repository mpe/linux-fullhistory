/*
 * Kernel Debugger Console I/O handler
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Scott Lurndal (slurn@engr.sgi.com)
 * Copyright (C) Scott Foehner (sfoehner@engr.sgi.com)
 * Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
 *
 * Written March 1999 by Scott Lurndal at Silicon Graphics, Inc.
 *
 * Modifications from:
 *	Chuck Fleckenstein		1999/07/20
 *		Move kdb_info struct declaration to this file
 *		for cases where serial support is not compiled into
 *		the kernel.
 *
 *	Masahiro Adegawa		1999/07/20
 *		Handle some peculiarities of japanese 86/106
 *		keyboards.
 *
 *	marc@mucom.co.il		1999/07/20
 *		Catch buffer overflow for serial input.
 *
 *      Scott Foehner  
 *              Port to ia64
 */

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/serial_reg.h>
#include <linux/spinlock.h>

#include <asm/io.h>

#include "pc_keyb.h"

int kdb_port = 0;

/*
 * This module contains code to read characters from the keyboard or a serial
 * port.
 * 
 * It is used by the kernel debugger, and is polled, not interrupt driven.
 *
 */

/*
 * send:  Send a byte to the keyboard controller.  Used primarily to 
 * 	  alter LED settings.
 */

static void
kdb_kbdsend(unsigned char byte) 
{
	while (inb(KBD_STATUS_REG) & KBD_STAT_IBF)
		;
	outb(KBD_DATA_REG, byte);
}

static void
kdb_kbdsetled(int leds)
{
	kdb_kbdsend(KBD_CMD_SET_LEDS);
	kdb_kbdsend((unsigned char)leds);
}

static void
console_read (char *buffer, size_t bufsize)
{
	struct console *in;
	struct console *out;
	char *cp, ch;

	for (in = console_drivers; in; in = in->next) {
		if ((in->flags & CON_ENABLED) && (in->read || in->wait_key))
			break;
	}
	for (out = console_drivers; out; out = out->next) {
		if ((out->flags & CON_ENABLED) && out->write)
			break;
	}

	if ((!in->read && !in->wait_key) || !out->write) {
		panic("kdb_io: can't do console i/o!");
	}

	if (in->read) {
		/* this is untested... */
		(*in->read)(in, buffer, bufsize);
		return;
	}

	bufsize -= 2;			/* leave room for CR & NUL terminator */
	cp = buffer;
	while (1) {
		ch = (*in->wait_key)(in);
		switch (ch) {
		      case '\b':
			if (cp > buffer) {
				--cp, ++bufsize;
				(*out->write)(out, "\b \b", 3);
			}
			break;

		      case '\025':
			while (cp > buffer) {
				--cp, ++bufsize;
				(*out->write)(out, "\b \b", 3);
			}
			break;

		      case '\r':
		      case '\n':
			(*out->write)(out, "\r\n", 2);
			*cp++ = '\n';
			*cp++ = '\0';
			return;

		      default:
			if (bufsize > 0) {
				(*out->write)(out, &ch, 1);
				--bufsize;
				*cp++ = ch;
			}
			break;
		}
	}
}

char *
kdb_getscancode(char *buffer, size_t bufsize)
{
	/*
	 * XXX Shouldn't kdb _always_ use console based I/O?  That's what the console
	 * abstraction is for, after all... ---davidm
	 */
#ifdef CONFIG_IA64_HP_SIM
	extern spinlock_t console_lock;
	unsigned long flags;

	spin_lock_irqsave(&console_lock, flags);
	console_read(buffer, bufsize);
	spin_unlock_irqrestore(&console_lock, flags);
	return buffer;
#else /* !CONFIG_IA64_HP_SIM */
	char	*cp = buffer;
	int	scancode, scanstatus;
	static int shift_lock = 0;	/* CAPS LOCK state (0-off, 1-on) */
	static int shift_key  = 0;	/* Shift next keypress */
	static int ctrl_key   = 0;
	static int leds       = 2;	/* Num lock */
	u_short keychar;
	extern u_short plain_map[], shift_map[], ctrl_map[];

	bufsize -= 2;	/* Reserve space for newline and null byte */

	/*
	 * If we came in via a serial console, we allow that to
	 * be the input window for kdb.
	 */
	if (kdb_port != 0) {
		char ch;
		int status;
#define serial_inp(info, offset) inb((info) + (offset))
#define serial_out(info, offset, v) outb((v), (info) + (offset))

		while(1) {
			while ((status = serial_inp(kdb_port, UART_LSR)) 
							& UART_LSR_DR) {
readchar:
				ch = serial_inp(kdb_port, UART_RX);
				if (ch == 8) {		/* BS */
					if (cp > buffer) {
						--cp, bufsize++;
						printk("%c %c", 0x08, 0x08);
					}
					continue;
				}
				serial_out(kdb_port, UART_TX, ch);
				if (ch == 13) {		/* CR */
					*cp++ = '\n';
					*cp++ = '\0';
					serial_out(kdb_port, UART_TX, 10);
					return(buffer);
				}
				/*
				 * Discard excess characters
				 */
				if (bufsize > 0) {
					*cp++ = ch;
					bufsize--;
				} 
			}
			while (((status = serial_inp(kdb_port, UART_LSR)) 
					& UART_LSR_DR) == 0);
		}
	}

	while (1) {

		/*
		 * Wait for a valid scancode
		 */

		while ((inb(KBD_STATUS_REG) & KBD_STAT_OBF) == 0)
			;

		/*
		 * Fetch the scancode
		 */
		scancode = inb(KBD_DATA_REG);
		scanstatus = inb(KBD_STATUS_REG);

		/*
		 * Ignore mouse events.
		 */
		if (scanstatus & KBD_STAT_MOUSE_OBF)
			continue;

		/*
		 * Ignore release, trigger on make
		 * (except for shift keys, where we want to 
		 *  keep the shift state so long as the key is
		 *  held down).
		 */

		if (((scancode&0x7f) == 0x2a) 
		 || ((scancode&0x7f) == 0x36)) {
			/*
			 * Next key may use shift table
			 */
			if ((scancode & 0x80) == 0) {
				shift_key=1;
			} else {
				shift_key=0;
			}
			continue;
		}

		if ((scancode&0x7f) == 0x1d) {
			/*
			 * Left ctrl key
			 */
			if ((scancode & 0x80) == 0) {
				ctrl_key = 1;
			} else {
				ctrl_key = 0;
			}
			continue;
		}

		if ((scancode & 0x80) != 0) 
			continue;

		scancode &= 0x7f;

		/*
	 	 * Translate scancode
		 */
		
		if (scancode == 0x3a) {
			/*
			 * Toggle caps lock
			 */
			shift_lock ^= 1;
			leds ^= 0x4;	/* toggle caps lock led */
			
			kdb_kbdsetled(leds);
			continue;
		}
			
		if (scancode == 0x0e) {
			/*
			 * Backspace
			 */
			if (cp > buffer) {
				--cp, bufsize++;

				/*
				 * XXX - erase character on screen
				 */
				printk("%c %c", 0x08, 0x08);
			}
			continue;
		}
	
		if (scancode == 0xe0) {
			continue;
		}

		/*
		 * For Japanese 86/106 keyboards 
		 * 	See comment in drivers/char/pc_keyb.c.
		 * 	- Masahiro Adegawa
		 */
		if (scancode == 0x73) {
			scancode = 0x59;
		} else if (scancode == 0x7d) {
			scancode = 0x7c;
		}
			
		if (!shift_lock && !shift_key) {
			keychar = plain_map[scancode];
		} else if (shift_lock || shift_key) {
			keychar = shift_map[scancode];
		} else if (ctrl_key) {
			keychar = ctrl_map[scancode];
		} else {
			keychar = 0x0020;
			printk("Unknown state/scancode (%d)\n", scancode);
		}
		 
		if ((scancode & 0x7f) == 0x1c) {
			/*
			 * enter key.  All done.
			 */
			printk("\n");
			break;
		}

		/*
		 * echo the character.
		 */
		printk("%c", keychar&0xff);

		if (bufsize) {
			--bufsize;
			*cp++ = keychar&0xff;
		} else {
			printk("buffer overflow\n");
			break;
		}

	}

	*cp++ = '\n';		/* White space for parser */
	*cp++ = '\0';		/* String termination */

#if defined(NOTNOW)
	cp = buffer;
	while (*cp) {
		printk("char 0x%x\n", *cp++);
	}
#endif

	return buffer;
#endif /* !CONFIG_IA64_HP_SIM */
}

