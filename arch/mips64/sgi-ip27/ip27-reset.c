/* $Id: ip27-reset.c,v 1.1 2000/01/17 23:32:47 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Reset an IP27.
 *
 * Copyright (C) 1997, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/sgialib.h>
#include <asm/sgi/sgihpc.h>
#include <asm/sgi/sgint23.h>

void machine_restart(char *command) __attribute__((noreturn));
void machine_halt(void) __attribute__((noreturn));
void machine_power_off(void) __attribute__((noreturn));

/* XXX How to pass the reboot command to the firmware??? */
void machine_restart(char *command)
{
	ArcReboot();
}

void machine_halt(void)
{
	ArcEnterInteractiveMode();
}

void machine_power_off(void)
{
	/* To do ...  */
}

void ip27_reboot_setup(void)
{
	/* Nothing to do on IP27.  */
}
