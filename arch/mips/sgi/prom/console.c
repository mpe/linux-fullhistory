/* $Id: console.c,v 1.1 1997/06/06 09:36:56 ralf Exp $
 * console.c: SGI arcs console code.
 *
 * Copyright (C) 1996 David S. Miller (dm@sgi.com)
 */

#include <asm/sgialib.h>

void prom_putchar(char c)
{
	long cnt;
	char it = c;

	romvec->write(1, &it, 1, &cnt);
}

char prom_getchar(void)
{
	long cnt;
	char c;

	romvec->read(0, &c, 1, &cnt);
	return c;
}
