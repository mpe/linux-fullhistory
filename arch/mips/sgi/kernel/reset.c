/*
 *  Reset a SGI.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998 by Ralf Baechle
 *
 * $Id: reset.c,v 1.3 1998/05/01 01:35:18 ralf Exp $
 */
#include <asm/io.h>
#include <asm/system.h>
#include <asm/reboot.h>
#include <asm/sgialib.h>

/* XXX How to pass the reboot command to the firmware??? */
void sgi_machine_restart(char *command)
{
	prom_reboot();
}

void sgi_machine_halt(void)
{
	prom_imode();
}

void sgi_machine_power_off(void)
{
	prom_powerdown();
}
