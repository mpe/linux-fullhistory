/* $Id: console.c,v 1.3 1999/11/19 23:29:05 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * ARC console code.
 *
 * Copyright (C) 1996 David S. Miller (dm@sgi.com)
 */
#include <linux/init.h>
#include <asm/sgialib.h>

void __init prom_putchar(char c)
{
	ULONG cnt;
	CHAR it = c;

	ArcWrite(1, &it, 1, &cnt);
}

char __init prom_getchar(void)
{
	ULONG cnt;
	CHAR c;

	ArcRead(0, &c, 1, &cnt);

	return c;
}
